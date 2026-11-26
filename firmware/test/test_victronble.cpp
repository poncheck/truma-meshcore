// Host-side end-to-end tests for Victron BLE decrypt+parse, ported from
// keshavdv/victron-ble's test suite. Test vectors (hex advertisement key,
// hex advertisement data, expected decoded values) are copied VERBATIM from
// tests/test_battery_monitor.py and tests/test_solar_charger.py in the
// reference repo -- not invented -- so a pass here means byte-exact parity
// with the real-world, widely-used Python implementation.

#include "../victronble.hpp"
#include "../victrondevices.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps, msg) do { \
    g_checks++; \
    double _a = (a); double _b = (b); \
    if (std::fabs(_a - _b) > eps) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s (got %f, expected %f)\n", __FILE__, __LINE__, msg, _a, _b); \
    } \
} while (0)

static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_cap) {
    size_t n = 0;
    while (hex[0] && hex[1] && n < out_cap) {
        auto hexval = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[n++] = (uint8_t)((hexval(hex[0]) << 4) | hexval(hex[1]));
        hex += 2;
    }
    return n;
}

void test_battery_monitor_end_to_end() {
    // From test_battery_monitor.py::test_end_to_end_parse
    uint8_t data[64], key[64];
    size_t data_len = hex_to_bytes("100289a302b040af925d09a4d89aa0128bdef48c6298a9", data, sizeof(data));
    size_t key_len = hex_to_bytes("aff4d0995b7d1e176c0c33ecb9e70dcd", key, sizeof(key));
    CHECK(key_len == 16, "key is 16 bytes");

    auto container = victronble::parse_container(data, data_len);
    CHECK((int)container.model_id == 0xa389, "model_id decodes to SmartShunt 500A/50mV (0xa389)");

    uint8_t plain[16];
    victronble::DecryptError err;
    size_t n = victronble::decrypt(container, key, plain, sizeof(plain), &err);
    CHECK(err == victronble::DecryptError::None, "decrypt succeeds with correct key");
    // 23-byte advertisement - 7-byte header = 16 bytes encrypted_data; minus
    // the 1-byte key-check prefix = 15 ciphertext bytes. The Python reference
    // PKCS7-pads this to a full 16-byte AES block before decrypting (AES-CTR
    // needs a full block internally), but the bit reader below only consumes
    // 118 bits (<=15 bytes), so 15 plaintext bytes is correct and sufficient
    // -- not a truncation bug.
    CHECK_NEAR((double)n, 15.0, 0.001, "plaintext length is 15 bytes (ciphertext length, not the padded-to-16 AES block)");

    auto reading = victrondevices::parse_battery_monitor(plain, n);
    CHECK(reading.aux_mode == victrondevices::AuxMode::DISABLED, "aux_mode == DISABLED");
    CHECK(reading.consumed_ah.has_value(), "consumed_ah present");
    CHECK_NEAR(*reading.consumed_ah, -50.0, 0.01, "consumed_ah == -50.0");
    CHECK(reading.current.has_value(), "current present");
    CHECK_NEAR(*reading.current, 0.0, 0.01, "current == 0");
    CHECK(!reading.remaining_mins.has_value(), "remaining_mins is None (0xFFFF sentinel)");
    CHECK(reading.soc.has_value(), "soc present");
    CHECK_NEAR(*reading.soc, 50.0, 0.01, "soc == 50.0");
    CHECK(reading.voltage.has_value(), "voltage present");
    CHECK_NEAR(*reading.voltage, 12.53, 0.01, "voltage == 12.53");
    CHECK(reading.alarm == 0, "alarm == NO_ALARM (0)");
    CHECK(!reading.temperature_celsius.has_value(), "temperature is None (aux_mode != TEMPERATURE)");
    CHECK(!reading.starter_voltage.has_value(), "starter_voltage is None");
    CHECK(!reading.midpoint_voltage.has_value(), "midpoint_voltage is None");
}

void test_battery_monitor_key_mismatch() {
    uint8_t data[64], key[64];
    size_t data_len = hex_to_bytes("100289a302bb01af129087600b9b97bc2c32867c8238da", data, sizeof(data));
    size_t key_len = hex_to_bytes("ffffffffffffffffffffffffffffffff", key, sizeof(key));
    CHECK(key_len == 16, "wrong key is 16 bytes");

    auto container = victronble::parse_container(data, data_len);
    uint8_t plain[16];
    victronble::DecryptError err;
    size_t n = victronble::decrypt(container, key, plain, sizeof(plain), &err);
    CHECK(n == 0, "decrypt returns 0 bytes on key mismatch");
    CHECK(err == victronble::DecryptError::KeyMismatch, "error is KeyMismatch");
}

void test_battery_monitor_aux_midpoint() {
    uint8_t decrypted[64];
    size_t n = hex_to_bytes("ffffe6040000feff010000000080fe0c", decrypted, sizeof(decrypted));
    auto reading = victrondevices::parse_battery_monitor(decrypted, n);
    CHECK(reading.midpoint_voltage.has_value(), "midpoint_voltage present");
    CHECK_NEAR(*reading.midpoint_voltage, 655.34, 0.01, "midpoint_voltage == 655.34");
}

void test_battery_monitor_aux_starter() {
    uint8_t decrypted[64];
    size_t n = hex_to_bytes("ffffe6040000feff000000000080feac", decrypted, sizeof(decrypted));
    auto reading = victrondevices::parse_battery_monitor(decrypted, n);
    CHECK(reading.starter_voltage.has_value(), "starter_voltage present");
    CHECK_NEAR(*reading.starter_voltage, -0.02, 0.01, "starter_voltage == -0.02");
}

void test_battery_monitor_aux_temperature() {
    uint8_t decrypted[64];
    size_t n = hex_to_bytes("ffffe6040000ffff020000000080fede", decrypted, sizeof(decrypted));
    auto reading = victrondevices::parse_battery_monitor(decrypted, n);
    CHECK(reading.temperature_celsius.has_value(), "temperature present");
    CHECK_NEAR(*reading.temperature_celsius, 382.2, 0.01, "temperature == 382.2 C (matches reference's raw kelvin_to_celsius output for this test vector)");
}

void test_solar_charger_end_to_end() {
    // From test_solar_charger.py::test_end_to_end_parse
    uint8_t data[64], key[64];
    size_t data_len = hex_to_bytes("100242a0016207adceb37b605d7e0ee21b24df5c", data, sizeof(data));
    size_t key_len = hex_to_bytes("adeccb947395801a4dd45a2eaa44bf17", key, sizeof(key));
    CHECK(key_len == 16, "key is 16 bytes");

    auto container = victronble::parse_container(data, data_len);
    CHECK((int)container.model_id == 0xa042, "model_id decodes to BlueSolar Charger MPPT 75/15 (0xa042)");

    uint8_t plain[16];
    victronble::DecryptError err;
    size_t n = victronble::decrypt(container, key, plain, sizeof(plain), &err);
    CHECK(err == victronble::DecryptError::None, "decrypt succeeds with correct key");

    auto reading = victrondevices::parse_solar_charger(plain, n);
    CHECK(reading.charge_state.has_value() && *reading.charge_state == 4, "charge_state == ABSORPTION (4)");
    CHECK(reading.battery_voltage.has_value(), "battery_voltage present");
    CHECK_NEAR(*reading.battery_voltage, 13.88, 0.01, "battery_voltage == 13.88");
    CHECK(reading.battery_charging_current.has_value(), "battery_charging_current present");
    CHECK_NEAR(*reading.battery_charging_current, 1.4, 0.01, "battery_charging_current == 1.4");
    CHECK(reading.yield_today_wh.has_value(), "yield_today present");
    CHECK_NEAR(*reading.yield_today_wh, 30.0, 0.01, "yield_today == 30");
    CHECK(reading.solar_power_w.has_value() && *reading.solar_power_w == 19, "solar_power == 19");
    CHECK(reading.external_device_load.has_value(), "external_device_load present");
    CHECK_NEAR(*reading.external_device_load, 0.0, 0.01, "external_device_load == 0.0");
}

void test_solar_charger_mppt100() {
    uint8_t decrypted[64];
    size_t n = hex_to_bytes("0300fb09650032000901ffff31bc45ad", decrypted, sizeof(decrypted));
    auto reading = victrondevices::parse_solar_charger(decrypted, n);
    CHECK(reading.battery_charging_current.has_value(), "battery_charging_current present");
    CHECK_NEAR(*reading.battery_charging_current, 10.1, 0.01, "battery_charging_current == 10.1");
    CHECK_NEAR(*reading.battery_voltage, 25.55, 0.01, "battery_voltage == 25.55");
    CHECK(reading.charge_state.has_value() && *reading.charge_state == 3, "charge_state == BULK (3)");
    CHECK(*reading.solar_power_w == 265, "solar_power == 265");
    CHECK_NEAR(*reading.yield_today_wh, 500.0, 0.01, "yield_today == 500");
    CHECK(!reading.external_device_load.has_value(), "external_device_load is None (0x1FF sentinel)");
}

int main() {
    test_battery_monitor_end_to_end();
    test_battery_monitor_key_mismatch();
    test_battery_monitor_aux_midpoint();
    test_battery_monitor_aux_starter();
    test_battery_monitor_aux_temperature();
    test_solar_charger_end_to_end();
    test_solar_charger_mppt100();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
