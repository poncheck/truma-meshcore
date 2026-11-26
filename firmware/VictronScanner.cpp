#include "VictronScanner.h"
#include "victronble.hpp"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <cstring>

namespace VictronScanner {

constexpr uint32_t STALE_AFTER_MS = 30000; // mark a reading stale after 30s with no update

static uint8_t s_shunt_mac[6];
static uint8_t s_shunt_key[16];
static uint8_t s_solar_mac[6];
static uint8_t s_solar_key[16];

static VictronReading s_shunt;
static VictronReading s_solar;

static bool mac_matches(BLEAddress addr, const uint8_t *mac) {
    // BLEAddress::toString() prints "aa:bb:cc:dd:ee:ff" in the same
    // human-readable order as our stored MACs; comparing via getNative()
    // bytes directly instead to avoid a string round-trip per advertisement.
    const uint8_t *native = addr.getNative();
    return memcmp(native, mac, 6) == 0;
}

static void handle_advertisement(const uint8_t *mfg_data, size_t mfg_len,
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

class VictronAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        bool is_shunt = mac_matches(advertisedDevice.getAddress(), s_shunt_mac);
        bool is_solar = !is_shunt && mac_matches(advertisedDevice.getAddress(), s_solar_mac);
        if (!is_shunt && !is_solar) return; // not one of ours

        if (!advertisedDevice.haveManufacturerData()) return;

        std::string mfg = advertisedDevice.getManufacturerData();
        if (mfg.size() <= 2) return;

        // First 2 bytes of BLE manufacturer-data are the company ID (0x02E1
        // for Victron, little-endian) -- victronble::parse_container expects
        // the data starting AFTER that company-id prefix (it reads its own
        // "prefix" field from what follows, per the Instant Readout format),
        // so skip exactly those 2 bytes here.
        const uint8_t *data = reinterpret_cast<const uint8_t *>(mfg.data());
        handle_advertisement(data + 2, mfg.size() - 2, is_shunt ? s_shunt_key : s_solar_key, is_shunt);
    }
};

static VictronAdvertisedDeviceCallbacks s_callbacks;
static BLEScan *s_scan = nullptr;

void begin(const uint8_t shunt_mac[6], const uint8_t shunt_key[16],
           const uint8_t solar_mac[6], const uint8_t solar_key[16]) {
    memcpy(s_shunt_mac, shunt_mac, 6);
    memcpy(s_shunt_key, shunt_key, 16);
    memcpy(s_solar_mac, solar_mac, 6);
    memcpy(s_solar_key, solar_key, 16);

    // NOTE: BLEDevice::init() must only be called ONCE per process. If this
    // board also runs a BLE companion interface (ENABLE_BLE_COMPANION-style
    // build), that code path's init must run first/instead and this begin()
    // must skip re-initialising -- verify which build variant applies
    // before wiring this into main.cpp's setup(). For the plain LoRa-mesh
    // sensor role (no BLE companion) targeted here, this is the only BLE
    // user, so a fresh init is correct.
    BLEDevice::init("");

    s_scan = BLEDevice::getScan();
    s_scan->setAdvertisedDeviceCallbacks(&s_callbacks, /*wantDuplicates=*/true);
    s_scan->setActiveScan(false); // passive scan -- matches Victron "Instant Readout" (no pairing/response needed)
    s_scan->setInterval(100);
    s_scan->setWindow(90);
    // 0 = scan indefinitely; callback-driven, loop() below just handles staleness.
    s_scan->start(0, nullptr, false);
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
