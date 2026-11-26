// Host-side tests for trumastatus.cpp (COMMAND_STATUS / id 0x33 payload
// parsing+serialization), verified against inetbox.py's own InetboxApp /
// TrumaCommand parser run on a synthetic-but-realistic 20-byte payload
// (printed by actually executing the reference implementation, not
// hand-derived) -- see the comment block with the exact byte sequence below.

#include "../trumastatus.hpp"
#include <cstdio>
#include <cstdint>
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

void test_parse_main_status() {
    // Generated and verified by running inetbox.py's real InetboxApp.COMMAND_STATUS.parse()
    // on this exact byte sequence (see project notes); expected decoded values:
    //   target_temp_room=21.0, heating_mode=eco(1), target_temp_water=40.0 (eco band),
    //   current_temp_water=38.5, current_temp_room=19.2, operating_status=5, error_code=0
    uint8_t payload[20] = {
        0x7c, 0x0b, 0x01, 0x00, 0x84, 0x03, 0x3a, 0x0c, 0x84, 0x03,
        0x01, 0x01, 0x2b, 0x0c, 0x6a, 0x0b, 0x05, 0x00, 0x00, 0x00
    };

    trumastatus::MainStatus status;
    bool ok = trumastatus::parse_main_status(payload, sizeof(payload), &status);
    CHECK(ok, "parse_main_status succeeds on a valid 20-byte payload");

    CHECK(status.target_temp_room_celsius.has_value(), "target_temp_room present");
    CHECK_NEAR(*status.target_temp_room_celsius, 21.0, 0.01, "target_temp_room == 21.0C");

    CHECK(status.heating_mode == trumastatus::HeatingMode::Eco, "heating_mode == Eco");

    CHECK(status.target_temp_water_celsius.has_value(), "target_temp_water present");
    CHECK_NEAR(*status.target_temp_water_celsius, 40.0, 0.01, "target_temp_water == 40.0C (eco band)");

    CHECK(status.current_temp_water_celsius.has_value(), "current_temp_water present");
    CHECK_NEAR(*status.current_temp_water_celsius, 38.5, 0.01, "current_temp_water == 38.5C");

    CHECK(status.current_temp_room_celsius.has_value(), "current_temp_room present");
    CHECK_NEAR(*status.current_temp_room_celsius, 19.2, 0.01, "current_temp_room == 19.2C");

    CHECK(status.operating_status == 5, "operating_status == 5 (On)");
    CHECK(status.error_code == 0, "error_code == 0 (no error)");
    CHECK(status.el_power_level == 900, "el_power_level == 900");
    CHECK(status.energy_mix == 1, "energy_mix == 1 (gas)");
}

void test_parse_rejects_short_payload() {
    uint8_t payload[10] = {0};
    trumastatus::MainStatus status;
    bool ok = trumastatus::parse_main_status(payload, sizeof(payload), &status);
    CHECK(!ok, "parse_main_status rejects a payload shorter than 20 bytes");
}

void test_serialize_round_trips_temperature() {
    trumastatus::MainStatus status;
    status.target_temp_room_celsius = 21.0;
    status.heating_mode = trumastatus::HeatingMode::Eco;
    status.target_temp_water_celsius = 40.0;
    status.el_power_level = 900;
    status.energy_mix = 1;

    uint8_t out[12];
    size_t n = trumastatus::serialize_main_status_write(status, out, sizeof(out));
    CHECK(n == 12, "serialize_main_status_write writes 12 bytes");

    // Byte-exact check against the first 12 bytes of our known-good 20-byte
    // reference payload (the read/write fields are laid out identically).
    uint8_t expected[12] = {
        0x7c, 0x0b, 0x01, 0x00, 0x84, 0x03, 0x3a, 0x0c, 0x84, 0x03, 0x01, 0x01
    };
    bool match = true;
    for (int i = 0; i < 12; i++) {
        if (out[i] != expected[i]) { match = false; break; }
    }
    CHECK(match, "serialized bytes match the reference-verified write-field layout exactly");
}

int main() {
    test_parse_main_status();
    test_parse_rejects_short_payload();
    test_serialize_round_trips_temperature();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
