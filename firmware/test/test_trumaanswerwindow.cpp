#include "../trumaanswerwindow.hpp"
#include <cstdio>
#include <cstdint>
#include <initializer_list>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

void test_active_at_boot() {
    trumaanswerwindow::AnswerWindow w;
    CHECK(w.is_active(0), "active at elapsed_ms=0");
    CHECK(!w.has_expired(), "not expired at elapsed_ms=0");
}

void test_active_just_before_window_end() {
    trumaanswerwindow::AnswerWindow w;
    CHECK(w.is_active(trumaanswerwindow::ANSWER_WINDOW_MS - 1), "active 1ms before window end");
    CHECK(!w.has_expired(), "not expired 1ms before window end");
}

void test_expires_exactly_at_window_end() {
    trumaanswerwindow::AnswerWindow w;
    CHECK(!w.is_active(trumaanswerwindow::ANSWER_WINDOW_MS), "inactive exactly at window end (>=, not >)");
    CHECK(w.has_expired(), "expired exactly at window end");
}

void test_stays_expired_forever_after(){
    trumaanswerwindow::AnswerWindow w;
    CHECK(!w.is_active(trumaanswerwindow::ANSWER_WINDOW_MS + 1000), "inactive well past window end");
    CHECK(w.has_expired(), "expired well past window end");
    // Simulate millis() wraparound (overflows back to a small value after
    // ~49.7 days) -- a naive re-check of elapsed_ms alone could wrongly
    // re-arm the window. The latch must prevent that.
    CHECK(!w.is_active(5), "does NOT re-arm even if a later elapsed_ms looks small (millis() wraparound safety)");
    CHECK(w.has_expired(), "still expired after simulated wraparound");
}

void test_never_queried_after_expiry_also_stays_expired() {
    // Expire it once via a large value, then confirm repeated queries at
    // varying elapsed_ms (including small ones) are all consistently false.
    trumaanswerwindow::AnswerWindow w;
    w.is_active(trumaanswerwindow::ANSWER_WINDOW_MS); // triggers expiry
    for (uint32_t t : {0u, 1u, 100u, 1000000u}) {
        CHECK(!w.is_active(t), "stays inactive regardless of subsequent elapsed_ms value");
    }
}

int main() {
    test_active_at_boot();
    test_active_just_before_window_end();
    test_expires_exactly_at_window_end();
    test_stays_expired_forever_after();
    test_never_queried_after_expiry_also_stays_expired();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
