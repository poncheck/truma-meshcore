#pragma once
// Port of inetbox.py's InetboxLINProtocol + Lin transport-layer state
// machine (lin.py's parse_transportlayer_master2slave /
// parse_transportlayer_slave2master / prepare_transportlayer_response /
// _answer_transportlayer_request), restricted to exactly what our iNet-Box
// slave role needs to answer:
//
//   SID 0xB9 (heartbeat)         -> single 0x00 byte reply
//   SID 0xB0 (assign NAD)        -> empty-payload ack
//   SID 0xBA (CP Plus requests upload FROM us) -> our status buffer, as a
//            multi-frame transport-layer response (header + up to 6 chunks)
//   SID 0xBB (CP Plus sends us a status download) -> ack + hands the raw
//            status-buffer bytes to trumastatus::parse_main_status
//   PID 0x18 ("D8 message")      -> FE/FF flag byte (FF = "I have an update
//            ready for you", gates whether CP Plus even bothers asking 0xBA)
//
// This class is deliberately transport-agnostic: it consumes already
// PID/checksum-validated transport-layer frame bytes (see trumalin.hpp for
// that lower layer) and produces reply frames into a caller-provided
// response queue. No UART/Serial calls anywhere in this file -- that keeps
// it host-testable with injected byte sequences, same pattern as
// trumalin/trumastatus.

#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include "trumalin.hpp"
#include "trumastatus.hpp"

namespace trumahandshake {

constexpr uint8_t NODE_ADDRESS = 0x03;
// The 4-byte "IDENTIFIER" inetbox.py uses to recognise messages addressed
// to an iNet-Box-role device (not a NAD/address -- a fixed protocol
// constant every real Truma iNet Box also uses, per the reference).
constexpr uint8_t IDENTIFIER[4] = {0x17, 0x46, 0x00, 0x1F};

constexpr size_t MAX_RESPONSE_FRAMES = 8; // single-ack cases need 1; upload (0xBA) needs up to 7
constexpr size_t FRAME_SIZE = 8;

struct ResponseQueue {
    std::array<std::array<uint8_t, FRAME_SIZE>, MAX_RESPONSE_FRAMES> frames{};
    size_t count = 0;

    bool push(const uint8_t *data, size_t len) {
        if (count >= MAX_RESPONSE_FRAMES || len > FRAME_SIZE) return false;
        auto &f = frames[count];
        f.fill(0xFF); // unused trailing bytes padded with 0xFF, matching the reference's style
        for (size_t i = 0; i < len; i++) f[i] = data[i];
        count++;
        return true;
    }
    bool empty() const { return count == 0; }
    // Pops the oldest queued frame into `out` (8 bytes). Returns false if empty.
    bool pop(uint8_t out[FRAME_SIZE]) {
        if (count == 0) return false;
        for (size_t i = 0; i < FRAME_SIZE; i++) out[i] = frames[0][i];
        for (size_t i = 1; i < count; i++) frames[i - 1] = frames[i];
        count--;
        return true;
    }
};

class Protocol {
public:
    // Call for every received transport-layer frame (PID 0x3C, master->slave)
    // once its header has already been parsed by trumalin::parse_transportlayer_frame_header.
    // `queue` receives any reply frame(s) to send back on PID 0x3D.
    void on_master_frame(const trumalin::TransportFrameHeader &header, ResponseQueue &queue);

    // Call when the CP Plus asks (PID 0x18) whether we have an update ready.
    // Returns the 8-byte reply payload (FE = "waiting", FF = "I have data for you").
    void get_pid18_reply(uint8_t out[8]) const;

    // Describes ONLY the field(s) a caller actually wants to change. All
    // fields optional/unset by default -- unset fields are left untouched
    // (carried over from the last known status), per queue_write()'s merge
    // behaviour below. This is the type CLI command handlers (`truma temp`,
    // `truma heating`, `truma boiler`) should build and pass to queue_write().
    struct WriteIntent {
        std::optional<double> target_temp_room_celsius;
        std::optional<trumastatus::HeatingMode> heating_mode;
        std::optional<double> target_temp_water_celsius;
        std::optional<uint16_t> el_power_level;
        std::optional<uint8_t> energy_mix;
    };

    // Queue a write: the next time CP Plus asks for an upload (SID 0xBA),
    // the merged status will be sent instead of just acking.
    //
    // SAFETY-CRITICAL MERGE BEHAVIOUR (matches inetbox.py's actual write
    // semantics, verified against the reference --
    // `{**self.status, **self.updates_to_send}` in
    // _get_status_buffer_for_writing): the Truma protocol's write buffer is
    // NOT a sparse "only change what I mention" update at the wire level --
    // every write resends ALL fields (target_temp_room, heating_mode,
    // target_temp_water, el_power_level, energy_mix). If a caller only sets
    // `intent.target_temp_room_celsius` and nothing else, this method fills
    // in every other field from `last_received_status()` (the most recent
    // real status received from CP Plus) before building the write buffer --
    // so the heater's other settings (boiler mode, energy mix, etc.) are
    // preserved exactly as they were, not reset to defaults.
    //
    // If `last_received_status()` has never been populated (no SID 0xBB
    // download has been received yet from a live CP Plus), any UNSET field
    // in `intent` falls back to MainStatus{}'s zero-value defaults (heating
    // off, water heater off, energy_mix 0) -- this is deliberately the safe
    // "everything off" state, not an attempt to guess the heater's real
    // current configuration. Do not call queue_write() with a sparse
    // intent before the read path is confirmed working (see README's
    // "Recommended bring-up order" -- read-only first, writes last).
    void queue_write(const WriteIntent &intent);
    bool has_pending_write() const { return pending_write_.has_value(); }

    // The most recent status received FROM the CP Plus (via SID 0xBB), if any.
    const std::optional<trumastatus::MainStatus>& last_received_status() const { return last_status_; }

    // Current command counter value (0-254, wraps per inetbox.py's `% 0xFF`).
    // Starts at 0; incremented BEFORE each write is sent (matches
    // `self.status["_command_counter"] = (self.status["_command_counter"] + 1) % 0xFF`
    // in inetbox.py's _get_status_buffer_for_writing, run on every write,
    // not just the first).
    uint8_t command_counter() const { return command_counter_; }

    // Optional callback invoked with a short, human-readable line (<=21
    // chars recommended, to fit the OLED display -- see trumalog.hpp) for
    // every significant protocol event: heartbeat, assign-NAD, status
    // received, write sent. Pure C function pointer (no std::function) to
    // stay lightweight and avoid heap allocation on the microcontroller.
    // Set to nullptr (the default) to disable -- this class has zero
    // Arduino/display dependency either way, logging is entirely optional.
    using LogCallback = void (*)(const char *line);
    void set_log_callback(LogCallback cb) { log_cb_ = cb; }

private:
    // Multi-frame reassembly state for an in-progress "first"+"consecutive" sequence.
    std::optional<uint8_t> reassembly_sid_;
    uint8_t reassembly_buf_[64] = {0};
    size_t reassembly_len_ = 0;
    size_t reassembly_expected_ = 0;

    std::optional<trumastatus::MainStatus> pending_write_;
    std::optional<trumastatus::MainStatus> last_status_;
    uint8_t command_counter_ = 0;
    LogCallback log_cb_ = nullptr;

    void log(const char *line) { if (log_cb_) log_cb_(line); }

    void complete_transportlayer_request(uint8_t sid, const uint8_t *payload, size_t len, ResponseQueue &queue);
};

} // namespace trumahandshake
