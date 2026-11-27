// LIN transceiver power-up sanity check -- BEFORE the transceiver's bus
// (LIN) pin is connected to the actual CI Bus/splitter/heater.
//
// At this stage only the microcontroller-side pins are wired:
//   T3 GPIO34 <- transceiver TX (data from transceiver to us)
//   T3 GPIO4  -> transceiver RX (data from us to transceiver)
//   GND/12V   -> transceiver power
// The transceiver's LIN pin itself is NOT yet connected to anything.
//
// What this sketch checks:
// 1. Raw digital level on GPIO34 (not as UART) -- most LIN transceivers
//    idle HIGH on their RXD/TX-to-host line when powered and the bus pin
//    is undriven/floating. A STABLE high (not floating/toggling noise)
//    is a good first sign the transceiver chip is alive and its output
//    driver is actively holding the line, not floating.
// 2. Sends test bytes out on Serial2 (GPIO4) and watches GPIO34 for any
//    reaction. With the LIN bus pin unconnected, most transceivers will
//    NOT echo TX back to RX (that's not how LIN works -- TX drives the
//    bus pin, RX reads the bus pin, they're not directly wired together
//    inside the chip) -- so "no echo" here is EXPECTED and not a failure.
//    This step is just to rule out anything unexpected (e.g. a dead
//    short between the two lines).

#include <Arduino.h>

HardwareSerial LinSerial(2);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== LIN transceiver power-up sanity check ===");
  Serial.println("(LIN bus pin NOT yet connected to CI Bus -- mcu-side only)");
  Serial.println();

  pinMode(34, INPUT);

  Serial.println("Reading raw digital level on GPIO34 (transceiver TX-to-us line)");
  Serial.println("for 3 seconds, sampling every 100ms:");
  for (int i = 0; i < 30; i++) {
    int level = digitalRead(34);
    Serial.print("  sample "); Serial.print(i);
    Serial.print(": "); Serial.println(level == HIGH ? "HIGH" : "LOW");
    delay(100);
  }

  Serial.println();
  Serial.println("Interpretation:");
  Serial.println("  - All HIGH, unchanging -> transceiver alive, output driver");
  Serial.println("    actively holding idle state. GOOD SIGN.");
  Serial.println("  - All LOW, unchanging -> could mean transceiver output is");
  Serial.println("    actively pulling low (check datasheet for this model's");
  Serial.println("    idle polarity), or transceiver not powered/not driving.");
  Serial.println("  - Random mix of HIGH/LOW -> floating pin, transceiver output");
  Serial.println("    not actively driving this line -- check wiring/power.");
  Serial.println();

  LinSerial.begin(19200, SERIAL_8N1, 34, 4); // LIN standard uses 19200 commonly, 9600 also common -- just testing transport, not real LIN framing yet
  Serial.println("Now sending test bytes on Serial2 (GPIO4) every 500ms,");
  Serial.println("watching for ANY received bytes on GPIO34 (not expected");
  Serial.println("without LIN bus connected -- just ruling out a direct short):");
}

void loop() {
  static uint32_t last_send = 0;
  if (millis() - last_send > 500) {
    LinSerial.write(0x55); // LIN sync byte, for realism
    last_send = millis();
  }

  if (LinSerial.available()) {
    uint8_t b = LinSerial.read();
    Serial.print("Unexpected byte received: 0x");
    Serial.println(b, HEX);
  }
}
