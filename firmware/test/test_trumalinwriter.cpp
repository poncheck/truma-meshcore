// Host-side tests for trumalinwriter.cpp -- builds the data+checksum bytes
// a LIN slave writes onto the bus. Expected checksum values verified by
// actually running inetbox.py's calculate_checksum on the same byte
// sequences (not eyeballed) -- see the comments on each test.

#include "../trumalinwriter.hpp"
#include "../trumalin.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>

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
        std::printf("FAIL [%s:%d] %s (got %d, expected %d)\n", __FILE__, __LINE__, msg, (int)_a, (int)_b); \
    } \
} while (0)

void test_classic_checksum_for_transportlayer_pid() {
    // Verified by running: calculate_checksum(bytes([0x03,0x02,0xF9,0x00,0xFF,0xFF,0xFF,0xFF])) -> 0x01
    uint8_t payload[8] = {0x03, 0x02, 0xF9, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t out[9];

    size_t n = trumalinwriter::build_answer_bytes(
        trumalin::PID_TRANSPORTLAYER_MASTER2SLAVE, 0xFF /* unused for classic checksum */,
        payload, out, sizeof(out));

    CHECK_EQ((int)n, 9, "build_answer_bytes writes 9 bytes");
    CHECK(memcmp(out, payload, 8) == 0, "first 8 bytes are the payload unchanged");
    CHECK_EQ((int)out[8], 0x01, "checksum matches the Python-verified classic checksum (0x01)");
}

void test_extended_checksum_for_non_transportlayer_pid() {
    // Verified by running: calculate_checksum(bytes([0xA0]) + bytes([0x11,0x22,...0x88])) -> 0xf8
    uint8_t payload[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t out[9];

    size_t n = trumalinwriter::build_answer_bytes(0x20 /* pid */, 0xA0 /* raw_pid, protected byte for 0x20 */,
                                                     payload, out, sizeof(out));

    CHECK_EQ((int)n, 9, "build_answer_bytes writes 9 bytes");
    CHECK(memcmp(out, payload, 8) == 0, "first 8 bytes are the payload unchanged");
    CHECK_EQ((int)out[8], 0xF8, "checksum matches the Python-verified extended checksum (0xF8)");
}

void test_out_capacity_too_small_returns_zero() {
    uint8_t payload[8] = {0};
    uint8_t out[5]; // deliberately too small
    size_t n = trumalinwriter::build_answer_bytes(trumalin::PID_TRANSPORTLAYER_SLAVE2MASTER, 0x3D,
                                                     payload, out, sizeof(out));
    CHECK_EQ((int)n, 0, "build_answer_bytes refuses to write into a too-small buffer");
}

int main() {
    test_classic_checksum_for_transportlayer_pid();
    test_extended_checksum_for_non_transportlayer_pid();
    test_out_capacity_too_small_returns_zero();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
