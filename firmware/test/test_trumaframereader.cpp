// Host-side tests for trumaframereader.cpp -- byte-stream LIN frame
// synchronization (break/sync/pid/data/checksum). Verifies both the happy
// path and the error paths (parity failure, checksum failure, resync after
// garbage bytes) using hand-built byte sequences whose checksums were
// computed with the SAME trumalin::calculate_checksum already verified
// against the Python reference elsewhere in this project (test_trumalin.cpp).

#include "../trumaframereader.hpp"
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

void test_valid_frame_transportlayer_master2slave() {
    // PID 0x3C protected: 0x3C already has valid parity bits per the LIN
    // spec examples used elsewhere in this project's test suite (see
    // test_trumalin.cpp's use of raw PID bytes like this). Data: 8 bytes
    // of a heartbeat-style single frame. Classic checksum covers only the
    // 8 data bytes (PID excluded), per inetbox.py's check_checksum call
    // for PID 0x3C/0x3D.
    uint8_t data[8] = {0x03, 0x02, 0xF9, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t checksum = trumalin::calculate_checksum(data, 8);

    uint8_t stream[11] = {0x00, 0x55, 0x3C, data[0], data[1], data[2], data[3],
                           data[4], data[5], data[6], data[7]};
    // Note: checksum appended separately below since stream[] above is only 11 bytes
    // (break+sync+pid+8data); checksum is the 12th byte.
    uint8_t full_stream[12];
    memcpy(full_stream, stream, 11);
    full_stream[11] = checksum;

    trumaframe::FrameReader reader;
    trumaframe::LinFrame frame;
    trumaframe::FeedResult result = trumaframe::FeedResult::Incomplete;
    for (size_t i = 0; i < sizeof(full_stream); i++) {
        result = reader.feed_byte(full_stream[i], &frame);
    }

    CHECK(result == trumaframe::FeedResult::FrameReady, "valid frame produces FrameReady");
    CHECK_EQ((int)frame.pid, 0x3C, "parsed PID is 0x3C (parity bits stripped)");
    CHECK(memcmp(frame.data, data, 8) == 0, "parsed data matches the original 8 bytes");
}

void test_checksum_error_detected() {
    uint8_t data[8] = {0x03, 0x02, 0xF9, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t wrong_checksum = trumalin::calculate_checksum(data, 8) ^ 0xFF; // deliberately wrong

    uint8_t stream[12] = {0x00, 0x55, 0x3C, data[0], data[1], data[2], data[3],
                           data[4], data[5], data[6], data[7], wrong_checksum};

    trumaframe::FrameReader reader;
    trumaframe::LinFrame frame;
    trumaframe::FeedResult result = trumaframe::FeedResult::Incomplete;
    for (size_t i = 0; i < sizeof(stream); i++) {
        result = reader.feed_byte(stream[i], &frame);
    }

    CHECK(result == trumaframe::FeedResult::ChecksumError, "wrong checksum is detected and rejected");
}

void test_parity_error_detected() {
    trumaframe::FrameReader reader;
    trumaframe::LinFrame frame;

    reader.feed_byte(0x00, &frame); // break
    reader.feed_byte(0x55, &frame); // sync
    trumaframe::FeedResult result = reader.feed_byte(0xFF, &frame); // garbage PID, bad parity

    CHECK(result == trumaframe::FeedResult::ParityError, "garbage PID byte 0xFF fails parity check");
}

void test_resyncs_after_garbage_before_break() {
    // Feed some random noise bytes BEFORE a valid break+sync+frame -- the
    // reader should ignore them and still find the valid frame that follows.
    uint8_t data[8] = {0x03, 0x01, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t checksum = trumalin::calculate_checksum(data, 8);

    uint8_t stream[15] = {
        0x12, 0x34, 0x56, // noise, not a break byte
        0x00, 0x55, 0x3C, // valid break+sync+pid
        data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
        checksum
    };

    trumaframe::FrameReader reader;
    trumaframe::LinFrame frame;
    trumaframe::FeedResult result = trumaframe::FeedResult::Incomplete;
    for (size_t i = 0; i < sizeof(stream); i++) {
        result = reader.feed_byte(stream[i], &frame);
    }

    CHECK(result == trumaframe::FeedResult::FrameReady, "reader resyncs past leading noise and finds the valid frame");
    CHECK_EQ((int)frame.pid, 0x3C, "parsed PID is correct after resync");
}

void test_extended_checksum_for_non_transportlayer_pid() {
    // For any PID other than 0x3C/0x3D, the checksum covers the PID byte too
    // (extended checksum), per inetbox.py's check_checksum(line[0:]) branch.
    // Use PID 0x20 (a real PID from inetbox.py's InetboxApp.handle_message
    // mapping for COMMAND_STATUS reporting). Parity bits computed via the
    // same formula as trumalin::check_pid_parity, verified by actually
    // running the Python-equivalent bit arithmetic (not eyeballed):
    //   id0..5 for 0x20 = 0,0,0,0,0,1 (only id5 set)
    //   P0 = id0^id1^id2^id4 = 0
    //   P1 = !(id1^id3^id4^id5) = !(1) = 0
    //   protected byte = 0x20 | (0<<6) | (0<<7) = 0x20 (parity bits both 0)
    uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t with_pid[9];
    with_pid[0] = 0x20; // protected PID byte for PID 0x20 (parity bits both zero)
    memcpy(&with_pid[1], data, 8);
    uint8_t checksum = trumalin::calculate_checksum(with_pid, 9);

    uint8_t stream[12] = {0x00, 0x55, 0x20, data[0], data[1], data[2], data[3],
                           data[4], data[5], data[6], data[7], checksum};

    trumaframe::FrameReader reader;
    trumaframe::LinFrame frame{};
    trumaframe::FeedResult result = trumaframe::FeedResult::Incomplete;
    for (size_t i = 0; i < sizeof(stream); i++) {
        result = reader.feed_byte(stream[i], &frame);
    }

    CHECK(result == trumaframe::FeedResult::FrameReady, "extended-checksum frame (non-transport PID) validates correctly");
    CHECK_EQ((int)frame.pid, 0x20, "parsed PID is 0x20");
}

void test_pid_detected_fires_before_data_bytes() {
    // The byte sequence is: break, sync, pid -- PidDetected must fire
    // exactly on the 3rd byte, with frame.pid already valid, before any
    // data bytes are fed. This is the hook a LIN slave implementation
    // uses to start writing its own response bytes onto the bus in sync
    // with the master's read timing (not exercised here -- this test only
    // verifies the notification itself fires at the right byte and with
    // the right value).
    trumaframe::FrameReader reader;
    trumaframe::LinFrame frame{};

    trumaframe::FeedResult r1 = reader.feed_byte(0x00, &frame); // break
    CHECK(r1 == trumaframe::FeedResult::Incomplete, "break byte alone is Incomplete");

    trumaframe::FeedResult r2 = reader.feed_byte(0x55, &frame); // sync
    CHECK(r2 == trumaframe::FeedResult::Incomplete, "sync byte alone is Incomplete");

    trumaframe::FeedResult r3 = reader.feed_byte(0x7D, &frame); // protected PID for 0x3D -- parity bits P0=1,P1=0 verified via the same formula trumalin::check_pid_parity implements (run explicitly, not guessed): 0x3D | (1<<6) | (0<<7) = 0x7D
    CHECK(r3 == trumaframe::FeedResult::PidDetected, "PidDetected fires immediately on the PID byte, before any data bytes");
    CHECK_EQ((int)frame.pid, 0x3D, "frame.pid is already correct at PidDetected time");
}

int main() {
    test_valid_frame_transportlayer_master2slave();
    test_checksum_error_detected();
    test_parity_error_detected();
    test_resyncs_after_garbage_before_break();
    test_extended_checksum_for_non_transportlayer_pid();
    test_pid_detected_fires_before_data_bytes();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
