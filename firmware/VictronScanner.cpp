#include "VictronScanner.h"
#include "victronble.hpp"
#include <bluefruit.h>
#include <cstring>

namespace VictronScanner {

constexpr uint32_t STALE_AFTER_MS = 30000; // mark a reading stale after 30s with no update

static BLEScanner scanner;

static uint8_t s_shunt_mac[6];
static uint8_t s_shunt_key[16];
static uint8_t s_solar_mac[6];
static uint8_t s_solar_key[16];

static VictronReading s_shunt;
static VictronReading s_solar;

static bool mac_matches(const uint8_t *addr, const uint8_t *mac) {
    return memcmp(addr, mac, 6) == 0;
}

static void handle_advertisement(const uint8_t *mfg_data, uint8_t mfg_len,
                                  const uint8_t *key, bool is_shunt) {
    if (mfg_len < 7) return; // too short to even contain the container header

    auto container = victronble::parse_container(mfg_data, mfg_len);

    uint8_t plain[16];
    victronble::DecryptError err;
    size_t n = victronble::decrypt(container, key, plain, sizeof(plain), &err);
    if (err != victronble::DecryptError::None || n == 0) {
        return; // wrong key, malformed packet, or genuinely not decryptable -- skip silently
    }

    if (is_shunt) {
        s_shunt.shunt = victrondevices::parse_battery_monitor(plain, n);
        s_shunt.fresh = true;
        s_shunt.last_seen_millis = millis();
    } else {
        s_solar.solar = victrondevices::parse_solar_charger(plain, n);
        s_solar.fresh = true;
        s_solar.last_seen_millis = millis();
    }
}

static void scan_callback(ble_gap_evt_adv_report_t *report) {
    const uint8_t *addr = report->peer_addr.addr; // little-endian, as broadcast

    bool is_shunt = mac_matches(addr, s_shunt_mac);
    bool is_solar = !is_shunt && mac_matches(addr, s_solar_mac);
    if (!is_shunt && !is_solar) {
        Bluefruit.Scanner.resume();
        return; // not one of ours
    }

    uint8_t mfg_data[31]; // max BLE advertisement payload size
    uint8_t mfg_len = Bluefruit.Scanner.parseReportByType(
        report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfg_data, sizeof(mfg_data));

    if (mfg_len > 2) {
        // First 2 bytes of BLE manufacturer-data are the company ID (0x02E1
        // for Victron, little-endian) -- victronble::parse_container expects
        // the data starting from AFTER that company-id prefix (it re-reads a
        // "prefix" field itself from what follows, per the Instant Readout
        // format), so skip exactly those 2 bytes here.
        handle_advertisement(&mfg_data[2], mfg_len - 2, is_shunt ? s_shunt_key : s_solar_key, is_shunt);
    }

    Bluefruit.Scanner.resume();
}

void begin(const uint8_t shunt_mac[6], const uint8_t shunt_key[16],
           const uint8_t solar_mac[6], const uint8_t solar_key[16]) {
    memcpy(s_shunt_mac, shunt_mac, 6);
    memcpy(s_shunt_key, shunt_key, 16);
    memcpy(s_solar_mac, solar_mac, 6);
    memcpy(s_solar_key, solar_key, 16);

    // NOTE: Bluefruit.begin()/Bluefruit.Scanner are assumed already
    // initialised by MeshCore's own BLE setup (companion radio BLE mode) if
    // this board also runs a BLE companion interface; if not (pure
    // LoRa-mesh sensor role, no BLE companion), this begin() call performs
    // the full Bluefruit stack init itself. Verify which applies once the
    // target `env:Xiao_nrf52_...` build variant is finalised -- calling
    // Bluefruit.begin() twice is undefined behaviour on this stack.
    Bluefruit.begin();
    Bluefruit.setTxPower(-12); // we only need scanning, not advertising -- keep TX low

    Bluefruit.Scanner.setRxCallback(scan_callback);
    Bluefruit.Scanner.useActiveScan(false); // passive scan -- matches Victron "Instant Readout" (no pairing/response needed)
    Bluefruit.Scanner.filterMSD(0x02E1); // Victron's BLE company identifier
    Bluefruit.Scanner.start(0); // 0 = scan forever
}

void loop() {
    uint32_t now = millis();
    if (s_shunt.fresh && (now - s_shunt.last_seen_millis) > STALE_AFTER_MS) {
        s_shunt.fresh = false;
    }
    if (s_solar.fresh && (now - s_solar.last_seen_millis) > STALE_AFTER_MS) {
        s_solar.fresh = false;
    }
}

const VictronReading& getShunt() { return s_shunt; }
const VictronReading& getSolar() { return s_solar; }

} // namespace VictronScanner
