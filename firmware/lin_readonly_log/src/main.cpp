// Truma LIN firmware -- PHASE 2: bounded, time-limited slave answering.
//
// SAFETY: this build DOES transmit on the LIN bus -- unlike the earlier
// listen-only build, it answers transport-layer requests (heartbeat,
// assign-NAD) on PID 0x3D. This is gated by TWO independent conditions,
// BOTH of which must hold or nothing is ever written to the wire:
//   1. trumaanswerwindow::AnswerWindow -- a ONE-WAY latch that permanently
//      disables answering 5 minutes after boot. There is no way to
//      re-arm it short of a physical power cycle (see
//      trumaanswerwindow.hpp, 14/14 host tests). This exists because the
//      device cannot be attended by a person watching both the CP Plus
//      display and a USB/serial connection at the same time -- once wired
//      into the heater and left running, nothing can react in real time
//      if something looks wrong, so the firmware bounds its own risk
//      window on its own.
//   2. response_queue must actually contain a reply, built from decoding
//      a real PID 0x3C master2slave frame via trumahandshake::Protocol --
//      we never write anything speculative or synthesized locally.
// Every write attempt (or deliberate non-write once the window has
// expired) is logged to the persistent SPIFFS log AND shown live via the
// "ANSWER"/"listen" indicator in the on-screen status footer -- always
// check that indicator against what you expect before leaving the
// device unattended on a live heater.
//
// What is answered: only SID 0xB9 (heartbeat) and SID 0xB0 (assign NAD),
// per trumahandshake::Protocol::on_master_frame(). NO temperature/
// heating/boiler writes are queued anywhere in this build -- `truma
// temp`/`heating`/`boiler` commands do not exist in this firmware yet.
// That remains a strictly later step per README's bring-up order, only
// after this bounded-answering step itself has been confirmed safe on a
// live CP Plus.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPIFFS.h>

#include "trumaframereader.hpp"
#include "trumalin.hpp"
#include "trumahandshake.hpp"
#include "trumalog.hpp"
#include "trumalinwriter.hpp"
#include "trumaanswerwindow.hpp"

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define LIN_RX_PIN 34  // transceiver TX -> us (data IN, input-only pin)
#define LIN_TX_PIN 4   // us -> transceiver RX (data OUT) -- used ONLY by
                        // the gated answer path in loop() (PidDetected
                        // case). HardwareSerial::begin() requires this pin
                        // regardless.
#define LIN_BAUD 9600

// Persistent log file (SPIFFS, survives reboot/power loss) -- added so
// results from an unattended test (device in the camper, no USB/laptop
// present) can be read back later over USB once reconnected, instead of
// only existing transiently in RAM (trumalog::LogRingBuffer, lost on
// reset) or on a serial monitor nobody has open at the time.
#define PERSIST_LOG_PATH "/trumalog.txt"
#define PERSIST_LOG_MAX_BYTES (200 * 1024) // ~half of this board's typical SPIFFS partition; if exceeded, the file is rotated (old content discarded) rather than growing unbounded or failing writes silently.

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
HardwareSerial LinSerial(2);

trumalog::LogRingBuffer log_buffer;
trumaframe::FrameReader frame_reader;
trumahandshake::Protocol protocol;
trumahandshake::ResponseQueue response_queue;
trumaanswerwindow::AnswerWindow answer_window;

static trumalog::LogRingBuffer *g_log_buffer_for_callback = nullptr;
static bool g_spiffs_ok = false;

// Appends one line to the persistent SPIFFS log file, prefixed with
// millis() so events can be placed in time even across the gap where
// nobody was watching. Rotates (truncates) the file if it would exceed
// PERSIST_LOG_MAX_BYTES -- this is a test/diagnostic log, not something
// that needs infinite history, and an unbounded file risks filling the
// flash partition during a long unattended run.
void persist_log_line(const char *line) {
    if (!g_spiffs_ok) return;

    File f = SPIFFS.open(PERSIST_LOG_PATH, FILE_APPEND);
    if (!f) return; // open-for-append creates the file if it doesn't exist yet

    if (f.size() > PERSIST_LOG_MAX_BYTES) {
        f.close();
        SPIFFS.remove(PERSIST_LOG_PATH);
        f = SPIFFS.open(PERSIST_LOG_PATH, FILE_APPEND);
        if (!f) return;
        f.println("--- log rotated (previous content discarded, exceeded size cap) ---");
    }

    f.printf("[%lu] %s\n", (unsigned long)millis(), line);
    f.close();
}

// Dumps the entire persistent log file to the USB serial port. Called once
// at boot (so reconnecting over USB immediately shows what happened while
// disconnected) -- this is the thing to read when the device comes back
// from the camper.
void dump_persist_log_to_serial() {
    if (!g_spiffs_ok) {
        Serial.println("SPIFFS not available -- no persistent log to show.");
        return;
    }
    if (!SPIFFS.exists(PERSIST_LOG_PATH)) {
        Serial.println("No persistent log file yet (first boot, or never wrote anything).");
        return;
    }
    File f = SPIFFS.open(PERSIST_LOG_PATH, FILE_READ);
    if (!f) {
        Serial.println("Persistent log file exists but failed to open for reading.");
        return;
    }
    Serial.println("=== BEGIN PERSISTENT LOG (/trumalog.txt) ===");
    while (f.available()) {
        Serial.write(f.read());
    }
    Serial.println("=== END PERSISTENT LOG ===");
    f.close();
}

void log_line_callback(const char *line) {
    if (g_log_buffer_for_callback) {
        g_log_buffer_for_callback->push_line(line);
    }
    persist_log_line(line);
    Serial.print("[trumahandshake] ");
    Serial.println(line);
}

void render_log() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("LIN PHASE2 (bounded TX)");

    constexpr int ROW_HEIGHT_PX = 8;
    constexpr int HEADER_ROWS = 1;
    constexpr int FOOTER_ROWS = 1; // reserved for the always-visible status line below
    int total_rows = SCREEN_HEIGHT / ROW_HEIGHT_PX;
    int visible_rows = total_rows - HEADER_ROWS - FOOTER_ROWS;
    if (visible_rows < 1) visible_rows = 1;

    size_t total = log_buffer.count();
    size_t show = (total < (size_t)visible_rows) ? total : (size_t)visible_rows;
    size_t start = total - show;

    for (size_t i = 0; i < show; i++) {
        display.setCursor(0, (int)(HEADER_ROWS + i) * ROW_HEIGHT_PX);
        display.print(log_buffer.get(start + i));
    }

    display.display();
}

static uint32_t frames_seen = 0;
static uint32_t checksum_errors = 0;
static uint32_t parity_errors = 0;

// Spinner character cycled every call -- drawn in the top-right corner,
// completely independent of the log/footer redraws below. If this isn't
// visibly spinning, the firmware has hung (crashed, stuck in an infinite
// loop, etc.) -- this is the single fastest "is it alive" check when
// nobody has USB/serial access (e.g. looking at the screen in the camper).
static const char SPINNER_CHARS[4] = {'|', '/', '-', '\\'};
static uint8_t spinner_index = 0;
static uint32_t last_spinner_update = 0;

void render_spinner() {
    constexpr int ROW_HEIGHT_PX = 8;
    constexpr int CHAR_WIDTH_PX = 6; // default Adafruit_GFX font width at text size 1
    int x = SCREEN_WIDTH - CHAR_WIDTH_PX;

    display.fillRect(x, 0, CHAR_WIDTH_PX, ROW_HEIGHT_PX, SSD1306_BLACK);
    display.setCursor(x, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.print(SPINNER_CHARS[spinner_index]);
    display.display();

    spinner_index = (spinner_index + 1) % 4;
}

// Draws the always-visible bottom status line (frame/error counts) --
// this is the single most important thing to glance at on the physical
// device with nobody watching the serial monitor: if `f` is stuck at 0
// after the transceiver is wired to a live CP Plus, something is wrong
// with the wiring/bus, not the firmware logic. Deliberately a SEPARATE
// draw call from render_log() so it's drawn fresh every time without
// needing to go through the scrolling log buffer (which could otherwise
// push it off-screen).
void render_status_footer() {
    char line[22];
    const char *mode = answer_window.is_active(millis()) ? "ANSWER" : "listen";
    snprintf(line, sizeof(line), "f=%lu cs=%lu p=%lu %s",
             (unsigned long)frames_seen, (unsigned long)checksum_errors, (unsigned long)parity_errors, mode);

    constexpr int ROW_HEIGHT_PX = 8;
    int footer_y = SCREEN_HEIGHT - ROW_HEIGHT_PX;

    // Clear just the footer row (avoid a full clearDisplay()+redraw of the
    // scrolling log on every call -- this runs every second).
    display.fillRect(0, footer_y, SCREEN_WIDTH, ROW_HEIGHT_PX, SSD1306_BLACK);
    display.setCursor(0, footer_y);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.print(line);
    display.display();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== Truma LIN Phase 2 firmware ===");
    Serial.printf("Bounded answering window: %lu ms from boot, then permanently listen-only.\n",
                   (unsigned long)trumaanswerwindow::ANSWER_WINDOW_MS);

    g_spiffs_ok = SPIFFS.begin(true); // true = format on first-ever mount failure
    if (g_spiffs_ok) {
        Serial.println("SPIFFS mounted OK -- persistent logging enabled.");
        dump_persist_log_to_serial(); // show whatever happened while disconnected
    } else {
        Serial.println("SPIFFS mount FAILED -- persistent logging disabled (RAM log + OLED still work).");
    }

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("OLED init FAILED -- check wiring/address");
        while (1) delay(1000);
    }
    Serial.println("OLED init OK");

    g_log_buffer_for_callback = &log_buffer;
    protocol.set_log_callback(log_line_callback);

    log_buffer.push_line("Boot OK, listening...");
    persist_log_line("=== BOOT (Phase 2: bounded answering) ==="); // marks a clean restart boundary in the persistent log
    render_log();
    render_status_footer();

    // NOTE: begin() requires a TX pin argument even though we never call
    // .write() on this instance in this read-only build -- the ESP32
    // UART peripheral needs both pins configured to initialize at all.
    LinSerial.begin(LIN_BAUD, SERIAL_8N1, LIN_RX_PIN, LIN_TX_PIN);
    Serial.printf("Serial2 listening: RX=GPIO%d TX=GPIO%d(unused) @ %d baud\n",
                  LIN_RX_PIN, LIN_TX_PIN, LIN_BAUD);
}

static uint32_t last_status_render = 0;

void loop() {
    while (LinSerial.available()) {
        uint8_t b = (uint8_t)LinSerial.read();
        trumaframe::LinFrame frame;
        trumaframe::FeedResult result = frame_reader.feed_byte(b, &frame);

        switch (result) {
            case trumaframe::FeedResult::PidDetected: {
                // This is the ONLY place this firmware ever writes to the
                // LIN bus. It only fires for PID 0x3D (slave->master transport
                // layer response slot) AND only while the answer window is
                // still active (first 5 minutes after boot -- see
                // trumaanswerwindow.hpp) AND only if we actually have a
                // queued reply (built from the PREVIOUS 0x3C master frame,
                // handled below in FrameReady). If any of those are false,
                // we do nothing and let the master2slave logic above just
                // observe whatever ends up on the bus, exactly like the
                // read-only build did.
                if (frame.pid == trumalin::PID_TRANSPORTLAYER_SLAVE2MASTER &&
                    answer_window.is_active(millis()) &&
                    !response_queue.empty()) {
                    uint8_t payload[8];
                    if (response_queue.pop(payload)) {
                        uint8_t out[9];
                        size_t n = trumalinwriter::build_answer_bytes(
                            frame.pid, 0x7D /* protected PID byte for 0x3D, parity-verified */,
                            payload, out, sizeof(out));
                        if (n == 9) {
                            LinSerial.write(out, n);
                            LinSerial.flush();
                            persist_log_line("TX: sent 0x3D reply");
                        }
                    }
                }
                break;
            }
            case trumaframe::FeedResult::FrameReady: {
                frames_seen++;
                char line[22];
                snprintf(line, sizeof(line), "PID=0x%02X seen (#%lu)", frame.pid, (unsigned long)frames_seen);
                log_buffer.push_line(line);
                persist_log_line(line);

                if (frame.pid == trumalin::PID_TRANSPORTLAYER_MASTER2SLAVE ||
                    frame.pid == trumalin::PID_TRANSPORTLAYER_SLAVE2MASTER) {
                    // DIAGNOSTIC ONLY (still 100% read-only -- no response is
                    // ever sent): decode and log the frame's node address +
                    // SID so we can confirm, before Phase 2 ever transmits
                    // anything, whether CP Plus is actually addressing
                    // heartbeat/assign-NAD requests to broadcast or to a
                    // specific NAD, and what SIDs are actually in play on
                    // this real bus.
                    auto header = trumalin::parse_transportlayer_frame_header(frame.data, 8);
                    char detail[22];
                    const char *dir = (frame.pid == trumalin::PID_TRANSPORTLAYER_MASTER2SLAVE) ? "M2S" : "S2M";
                    const char *addr_str = (header.node_address == trumalin::NODE_ADDRESS_BROADCAST) ? "bcast" : "addr";
                    snprintf(detail, sizeof(detail), "%s %s=0x%02X sid=0x%02X", dir, addr_str,
                             header.node_address, header.sid);
                    log_buffer.push_line(detail);
                    persist_log_line(detail);

                    if (frame.pid == trumalin::PID_TRANSPORTLAYER_MASTER2SLAVE) {
                        // Populate the GLOBAL response_queue so the very next
                        // PID 0x3D slot (handled above in PidDetected) can
                        // pop and send it -- gated by answer_window so this
                        // can only ever happen in the first 5 minutes after
                        // boot (see trumaanswerwindow.hpp for the permanent,
                        // one-way cutoff).
                        protocol.on_master_frame(header, response_queue);
                        if (!response_queue.empty()) {
                            const char *note = answer_window.is_active(millis())
                                ? "queued reply (will send)"
                                : "queued reply (window expired, NOT sent)";
                            persist_log_line(note);
                        }
                    } else {
                        // frame.pid == PID_TRANSPORTLAYER_SLAVE2MASTER: this
                        // is our own transmitted reply looping back through
                        // the transceiver onto our RX line (normal on a
                        // shared LIN bus -- inetbox.py explicitly discards
                        // this same loopback via reset_input_buffer() after
                        // every answer it sends). Nothing to do here besides
                        // the diagnostic log line already emitted above;
                        // this frame carries no new information.
                    }
                }
                render_log();
                render_status_footer();
                break;
            }
            case trumaframe::FeedResult::ChecksumError:
                checksum_errors++;
                persist_log_line("ChecksumError");
                render_status_footer();
                break;
            case trumaframe::FeedResult::ParityError:
                parity_errors++;
                persist_log_line("ParityError");
                render_status_footer();
                break;
            case trumaframe::FeedResult::Incomplete:
                break;
        }
    }

    // Always-visible footer is kept fresh at least once a second even with
    // zero bus activity, so the counters are never stale on-screen -- this
    // is the thing to photograph/describe when nobody has USB/serial
    // access to the device (e.g. once it's installed in the camper).
    if (millis() - last_status_render > 1000) {
        render_status_footer();
        char idle_line[40];
        snprintf(idle_line, sizeof(idle_line), "idle f=%lu cs=%lu p=%lu",
                 (unsigned long)frames_seen, (unsigned long)checksum_errors, (unsigned long)parity_errors);
        Serial.println(idle_line);
        last_status_render = millis();
    }

    // A periodic "still alive, here's the running totals" line in the
    // PERSISTENT log too -- not every second (that would fill the file
    // fast over a multi-hour unattended run), but often enough that a gap
    // with no "idle" entries at all would itself be a clear sign the
    // device lost power or crashed at some point while unattended.
    static uint32_t last_persist_idle = 0;
    if (millis() - last_persist_idle > 60000) {
        char idle_line[40];
        snprintf(idle_line, sizeof(idle_line), "idle f=%lu cs=%lu p=%lu",
                 (unsigned long)frames_seen, (unsigned long)checksum_errors, (unsigned long)parity_errors);
        persist_log_line(idle_line);
        last_persist_idle = millis();
    }

    // Spinner updates independently and more frequently than the footer --
    // this is the "firmware definitely isn't hung" indicator, visible at a
    // glance without reading any numbers.
    if (millis() - last_spinner_update > 300) {
        render_spinner();
        last_spinner_update = millis();
    }
}
