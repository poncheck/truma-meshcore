#include "../trumalog.hpp"
#include <cstdio>
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

void test_basic_push_and_get() {
    trumalog::LogRingBuffer buf;
    buf.push_line("hello");
    buf.push_line("world");

    CHECK(buf.count() == 2, "count is 2 after two pushes");
    CHECK(strcmp(buf.get(0), "hello") == 0, "oldest line is 'hello'");
    CHECK(strcmp(buf.get(1), "world") == 0, "newest line is 'world'");
}

void test_truncates_long_lines() {
    trumalog::LogRingBuffer buf;
    buf.push_line("this line is definitely way too long for the OLED display width limit");

    CHECK(strlen(buf.get(0)) == trumalog::MAX_LINE_LEN - 1, "long line truncated to MAX_LINE_LEN-1 chars");
}

void test_wraps_when_full() {
    trumalog::LogRingBuffer buf;
    char tmp[8];
    for (int i = 0; i < (int)trumalog::MAX_LINES + 3; i++) {
        snprintf(tmp, sizeof(tmp), "L%d", i);
        buf.push_line(tmp);
    }

    CHECK(buf.count() == trumalog::MAX_LINES, "count caps at MAX_LINES after overflow");
    // Oldest surviving line should be "L3" (lines L0, L1, L2 were evicted
    // after MAX_LINES+3 pushes with a MAX_LINES-capacity ring).
    char expected_oldest[8];
    snprintf(expected_oldest, sizeof(expected_oldest), "L%d", 3);
    CHECK(strcmp(buf.get(0), expected_oldest) == 0, "oldest line after wraparound is the correct one");

    char expected_newest[8];
    snprintf(expected_newest, sizeof(expected_newest), "L%d", (int)trumalog::MAX_LINES + 2);
    CHECK(strcmp(buf.get(buf.count() - 1), expected_newest) == 0, "newest line after wraparound is the correct one");
}

void test_out_of_range_get_returns_empty() {
    trumalog::LogRingBuffer buf;
    buf.push_line("only one line");
    CHECK(strcmp(buf.get(5), "") == 0, "out-of-range get() returns empty string, not garbage/crash");
}

int main() {
    test_basic_push_and_get();
    test_truncates_long_lines();
    test_wraps_when_full();
    test_out_of_range_get_returns_empty();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
