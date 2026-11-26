#pragma once
// nRF52/Bluefruit BLE scanning glue for Victron "Instant Readout" advertisements.
// Wraps our host-tested victronble::decrypt + victrondevices parsers --
// this file itself is Arduino/Bluefruit-specific and therefore NOT part of
// the host test suite (no g++ unit tests here), but it deliberately contains
// as little logic as possible: just extract manufacturer-data bytes from the
// BLE scan report and MAC filter, then hand off to the already-verified
// pure-logic layer.
//
// Usage (wired up in main.cpp):
//   VictronScanner::begin();            // call once in setup()
//   VictronScanner::loop();             // call every loop() iteration (cheap, just checks staleness)
//   VictronScanner::getShunt()          // -> const VictronReading& (check .fresh)
//   VictronScanner::getSolar()          // -> const VictronReading&

#include <cstdint>
#include <optional>
#include "victrondevices.hpp"

struct VictronReading {
    bool fresh = false;          // true if updated within VICTRON_STALE_SECONDS
    uint32_t last_seen_millis = 0;
    victrondevices::BatteryMonitorReading shunt{};   // valid only for the SmartShunt device
    victrondevices::SolarChargerReading solar{};     // valid only for the SmartSolar device
};

namespace VictronScanner {

// `shunt_mac`/`solar_mac` are 6-byte arrays, little-endian as reported by
// Bluefruit's ble_gap_addr_t.addr (same byte order as advertised over the
// air -- verify against `VICTRON_SMARTSHUNT_MAC`/`VICTRON_SMARTSOLAR_MAC`
// from ~/.hermes/.env, which are in human-readable AA:BB:CC:DD:EE:FF form
// and must be reversed to little-endian byte order when hardcoding here).
void begin(const uint8_t shunt_mac[6], const uint8_t shunt_key[16],
           const uint8_t solar_mac[6], const uint8_t solar_key[16]);

void loop(); // marks readings stale if no update for too long

const VictronReading& getShunt();
const VictronReading& getSolar();

} // namespace VictronScanner
