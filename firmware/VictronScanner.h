#pragma once
// ESP32 (Bluedroid "BLEDevice" library) BLE scanning glue for Victron
// "Instant Readout" advertisements, for the LilyGO T3_V1.6.1
// (ESP32 + SX1276) target. Wraps our host-tested victronble::decrypt +
// victrondevices parsers -- this file itself is Arduino/ESP32-BLEDevice
// specific and therefore NOT part of the host test suite (no g++ unit
// tests here), but deliberately contains as little logic as possible: just
// extract manufacturer-data bytes from the BLE scan callback and MAC
// filter, then hand off to the already-verified pure-logic layer.
//
// NOTE: this supersedes the earlier nRF52/Bluefruit version of this file
// (VictronScanner for xiao_nrf52) -- that target was dropped in favour of
// the LilyGO T3_V1.6.1 (see project notes, 2026-11-26). The nRF52 variant
// is kept in git history but no longer the active target.
//
// Usage (wired up in main.cpp):
//   VictronScanner::begin();   // call once in setup(), AFTER MeshCore's own
//                               // BLEDevice::init() if this board ALSO runs
//                               // a BLE companion interface -- BLEDevice::init()
//                               // must only be called once. For the plain
//                               // LoRa-mesh sensor role (no BLE companion),
//                               // this begin() performs that init itself.
//   VictronScanner::loop();    // call every loop() iteration (cheap, just checks staleness + restarts scan)
//   VictronScanner::getShunt() // -> const VictronReading&
//   VictronScanner::getSolar() // -> const VictronReading&

#include <cstdint>
#include "victrondevices.hpp"

struct VictronReading {
    bool fresh = false;
    uint32_t last_seen_millis = 0;
    victrondevices::BatteryMonitorReading shunt{};
    victrondevices::SolarChargerReading solar{};
};

namespace VictronScanner {

// MAC bytes as printed by BLE (human order, e.g. the same order as shown in
// the Victron Connect app / `VICTRON_SMARTSHUNT_MAC` -- NOT pre-reversed;
// BLEAddress comparison is done in whatever order ESP32's BLEAddress::equals
// uses internally, verified once against real hardware, not assumed here).
void begin(const uint8_t shunt_mac[6], const uint8_t shunt_key[16],
           const uint8_t solar_mac[6], const uint8_t solar_key[16]);

void loop();

const VictronReading& getShunt();
const VictronReading& getSolar();

} // namespace VictronScanner
