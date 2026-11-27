#pragma once
// Pure logic for the Phase 2 safety cutoff: answering on the LIN bus is
// only ever allowed for a fixed window after boot, after which it must
// permanently fall back to listen-only -- with NO way to re-arm it short
// of a physical power cycle. This exists because the hardware itself
// cannot be attended by a person watching both the CP Plus display and
// this device's serial/USB at the same time (confirmed operational
// constraint): once the board is wired into the heater and left running
// unattended, nothing can react in real time if something goes wrong, so
// the firmware must bound its own risk window on its own.
//
// No Arduino/millis() dependency here -- the caller passes the elapsed
// time, this just decides yes/no. Kept deliberately trivial and
// exhaustively tested because a bug here (e.g. integer overflow, off-by-
// one re-arming the window) directly controls how long the device is
// allowed to write onto a live heater's control bus unattended.

#include <cstdint>

namespace trumaanswerwindow {

// 5 minutes, matching the plan agreed with the user: short enough that an
// unattended mistake can't run for a full car/camper visit, long enough
// to actually observe a handful of heartbeat/assign-NAD cycles.
constexpr uint32_t ANSWER_WINDOW_MS = 5UL * 60UL * 1000UL;

class AnswerWindow {
public:
    // `elapsed_ms` is time since boot (e.g. millis()). Once this returns
    // false for a given elapsed_ms, it is guaranteed to return false for
    // all larger elapsed_ms too (monotonic one-way latch) -- expiry is
    // permanent for the lifetime of this object, there is no code path
    // that can make is_active() true again after it has gone false.
    bool is_active(uint32_t elapsed_ms) {
        if (expired_) return false;
        if (elapsed_ms >= ANSWER_WINDOW_MS) {
            expired_ = true;
            return false;
        }
        return true;
    }

    bool has_expired() const { return expired_; }

private:
    bool expired_ = false;
};

} // namespace trumaanswerwindow
