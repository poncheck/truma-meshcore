// Minimal raw UART2 byte dump -- bypasses trumaframereader entirely.
// Prints every single raw byte received on GPIO34 (RX) via HardwareSerial(2).
// Purpose: isolate whether the UART peripheral itself sees ANY bytes while
// LIN is shorted to GND (which produces a stable, confirmed voltage drop
// per the GPIO34 raw ADC probe -- this test checks if that translates into
// UART-framed bytes at all, independent of trumaframereader's logic).

#include <Arduino.h>

#define LIN_RX_PIN 34
#define LIN_TX_PIN 4
#define LIN_BAUD 9600

HardwareSerial LinSerial(2);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== Raw UART2 byte dump (no frame parsing) ===");
    LinSerial.begin(LIN_BAUD, SERIAL_8N1, LIN_RX_PIN, LIN_TX_PIN);
    Serial.printf("Serial2: RX=GPIO%d TX=GPIO%d @ %d baud\n", LIN_RX_PIN, LIN_TX_PIN, LIN_BAUD);
    Serial.println("Printing every byte received (hex). Silence = UART sees nothing.");
}

uint32_t byte_count = 0;
uint32_t last_report = 0;

void loop() {
    while (LinSerial.available()) {
        uint8_t b = (uint8_t)LinSerial.read();
        byte_count++;
        Serial.printf("RAW BYTE: 0x%02X (total=%lu)\n", b, (unsigned long)byte_count);
    }
    if (millis() - last_report > 2000) {
        Serial.printf("alive, total_bytes_seen=%lu\n", (unsigned long)byte_count);
        last_report = millis();
    }
}
