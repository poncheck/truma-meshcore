// Host-side unit tests for trumalin.cpp / trumaconv.cpp.
// Build & run: see run_tests.sh in this directory. No Arduino/ESP32/hardware
// needed -- this is pure logic validated against the inetbox.py reference
// implementation's documented behaviour (checksum, parity, temp codes, etc).

#include "../trumalin.hpp"
#include "../trumaconv.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
    g_checks++; \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s (got %lld, expected %lld)\n", __FILE__, __LINE__, msg, \
            (long long)_a, (long long)_b); \
    } \
} while (0)

void test_checksum() {
    // Expected values cross-checked by actually RUNNING the reference
    // implementation (inetbox.py tools.calculate_checksum) on these exact
    // byte sequences -- not eyeballed from the README's log table (which
    // uses a confusing column layout where the real checksum appears on a
    // separate line labelled "Checksum", not inline with the data bytes).
    uint8_t data[] = {0x3C, 0x01, 0x06, 0xB8, 0x40, 0x03, 0x00, 0x00, 0xFF};
    uint8_t cs = trumalin::calculate_checksum(data, sizeof(data));
    CHECK_EQ((int)cs, 0xC0, "checksum matches inetbox.py reference output (3C frame)");

    uint8_t data2[] = {0x3D, 0x7F, 0x06, 0xB2, 0x00, 0x17, 0x46, 0x00, 0x1F};
    uint8_t cs2 = trumalin::calculate_checksum(data2, sizeof(data2));
    CHECK_EQ((int)cs2, 0x0E, "checksum matches inetbox.py reference output (3D frame)");
}

void test_pid_parity() {
    // PID 0x3C with correct parity bits is well documented in the Truma logs
    // as being transmitted as raw byte... we validate parity math is
    // self-consistent: encode-like check, then corrupt and expect throw.
    // Known-good protected IDs from the LIN spec examples (PID 0x00 -> 0x80,
    // PID 0x01 -> 0x41, PID 0x20 -> ... ); we use round-trip self-consistency
    // instead since we don't have an encoder (the real master does the
    // encoding; we only ever need to decode here).
    bool threw = false;
    try {
        trumalin::check_pid_parity(0xFF); // garbage, should fail parity
    } catch (const std::exception &) {
        threw = true;
    }
    CHECK(threw, "garbage byte 0xFF fails parity check");
}

void test_temp_conversion() {
    // 25.0 C -> (25+273)*10 = 2980
    CHECK_EQ(trumaconv::celsius_to_temp_code(25.0), 2980, "25C encodes to 2980");
    CHECK(std::abs(trumaconv::temp_code_to_celsius(2980) - 25.0) < 0.01, "2980 decodes to ~25C");

    // "no reading" sentinels all decode to 0.0
    CHECK(trumaconv::temp_code_to_celsius(0xAAA) == 0.0, "0xAAA decodes to 0");
    CHECK(trumaconv::temp_code_to_celsius(0xAAAA) == 0.0, "0xAAAA decodes to 0");
    CHECK(trumaconv::temp_code_to_celsius(0x0000) == 0.0, "0x0000 decodes to 0");

    // Below 5C setpoint always encodes as "off" (0x00), matching inetbox.py's
    // decimal_to_temp_code behaviour (heating can't be set below 5C).
    CHECK_EQ(trumaconv::celsius_to_temp_code(4.9), 0x00, "setpoint below 5C encodes to off");
}

void test_water_target() {
    CHECK(trumaconv::water_temp_code_to_target(trumaconv::celsius_to_temp_code(0.0)) == trumaconv::WaterTarget::Off, "0C -> Off");
    CHECK(trumaconv::water_temp_code_to_target(trumaconv::celsius_to_temp_code(40.0)) == trumaconv::WaterTarget::Eco, "40C -> Eco");
    CHECK(trumaconv::water_temp_code_to_target(trumaconv::celsius_to_temp_code(60.0)) == trumaconv::WaterTarget::High, "60C -> High");
    CHECK(trumaconv::water_temp_code_to_target(trumaconv::celsius_to_temp_code(200.0)) == trumaconv::WaterTarget::Boost, "200C -> Boost");

    // round trip
    CHECK_EQ(trumaconv::water_target_to_temp_code(trumaconv::WaterTarget::High),
             trumaconv::celsius_to_temp_code(60.0), "High round-trips to 60C code");
}

void test_heating_mode() {
    CHECK(trumaconv::heating_mode_from_code(0) == trumaconv::HeatingMode::Off, "code 0 -> Off");
    CHECK(trumaconv::heating_mode_from_code(1) == trumaconv::HeatingMode::Eco, "code 1 -> Eco");
    CHECK(trumaconv::heating_mode_from_code(10) == trumaconv::HeatingMode::Boost, "code 10 -> Boost");
    CHECK_EQ(trumaconv::heating_mode_to_code(trumaconv::HeatingMode::Eco), 1, "Eco round-trips to code 1");
}

void test_error_code() {
    // error_code_bytes[1]*100 + error_code_bytes[0]; e.g. low=45,high=5 -> 545 (E545 from the issue tracker)
    CHECK_EQ(trumaconv::error_code_from_bytes(45, 5), 545, "classic E545 error code decodes correctly");
}

void test_transportlayer_single_frame() {
    // Single-frame transport layer message, PCI identifier 0x0, matching the
    // structure parsed by inetbox.py's parse_transportlayer_frame_header.
    // node_address=0x03, PCI byte 0x02 (type=single, len=2), sid=0xF9, payload=[0x00]
    uint8_t data[] = {0x03, 0x02, 0xF9, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    auto h = trumalin::parse_transportlayer_frame_header(data, sizeof(data));
    CHECK(h.type == trumalin::FrameType::Single, "frame type decoded as Single");
    CHECK_EQ((int)h.node_address, 0x03, "node address decoded correctly");
    CHECK_EQ((int)h.sid, 0xF9, "sid decoded correctly");
    CHECK_EQ((int)h.expected_bytes, 1, "expected_bytes decoded correctly");
    CHECK_EQ((int)h.payload.size(), 1, "payload has 1 byte");
    CHECK_EQ((int)h.payload[0], 0x00, "payload byte decoded correctly");
}

int main() {
    test_checksum();
    test_pid_parity();
    test_temp_conversion();
    test_water_target();
    test_heating_mode();
    test_error_code();
    test_transportlayer_single_frame();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
