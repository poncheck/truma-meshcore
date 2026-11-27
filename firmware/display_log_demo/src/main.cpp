// Standalone demo: shows the Truma/Victron activity log (trumalog ring
// buffer) on the T3's OLED, with fake/simulated log lines appended every
// few seconds -- just to visually confirm the display + scrolling log
// rendering works BEFORE wiring it into the full LoRa+LIN firmware.
//
// This is a throwaway demo (same category as lin_probe/uart_probe earlier
// in this project) -- the canonical, host-tested trumalog/TrumaLogDisplay
// code lives in ~/.hermes/apps/truma-meshcore/firmware/, this just proves
// the OLED wiring + Adafruit_SSD1306 library work on this physical board
// before integrating with the real LoRa/LIN logic.
//
// OLED: SDA=GPIO21, SCL=GPIO22 (confirmed matching this physical T3 unit),
// I2C address 0x3C (standard for 128x64 SSD1306), reset pin -1 (shared
// with Arduino reset -- MeshCore's own default of reset=21 collides with
// SDA on this pin assignment, so deliberately NOT reusing that here).

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "trumalog.hpp"

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
trumalog::LogRingBuffer log_buffer;

void render_log() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("TRUMA/VICTRON LOG");

    constexpr int ROW_HEIGHT_PX = 8;
    constexpr int HEADER_ROWS = 1;
    int visible_rows = (SCREEN_HEIGHT / ROW_HEIGHT_PX) - HEADER_ROWS;

    size_t total = log_buffer.count();
    size_t show = (total < (size_t)visible_rows) ? total : (size_t)visible_rows;
    size_t start = total - show;

    for (size_t i = 0; i < show; i++) {
        display.setCursor(0, (int)(HEADER_ROWS + i) * ROW_HEIGHT_PX);
        display.print(log_buffer.get(start + i));
    }

    display.display();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== Display + activity log demo ===");

    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("OLED init FAILED -- check wiring/address");
        while (1) delay(1000);
    }
    Serial.println("OLED init OK");

    log_buffer.push_line("Display demo booted");
    log_buffer.push_line("Waiting for data...");
    render_log();
}

void loop() {
    static uint32_t last_line = 0;
    static int counter = 0;

    if (millis() - last_line > 2000) {
        char line[22];
        // Simulate the kind of lines the real firmware will produce
        // (RX heartbeat / RX room=.. wtr=.. / TX room=.. #N), alternating,
        // just to exercise the scrolling behavior with realistic content.
        switch (counter % 3) {
            case 0:
                snprintf(line, sizeof(line), "RX heartbeat #%d", counter);
                break;
            case 1:
                snprintf(line, sizeof(line), "RX room=%.1f wtr=%.1f", 19.0 + (counter % 5) * 0.3, 38.0 + (counter % 3));
                break;
            default:
                snprintf(line, sizeof(line), "TX room=21.0 #%d", counter);
                break;
        }
        log_buffer.push_line(line);
        render_log();
        Serial.print("logged: "); Serial.println(line);
        counter++;
        last_line = millis();
    }
}
