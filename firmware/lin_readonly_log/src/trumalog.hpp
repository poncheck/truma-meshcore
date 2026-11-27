#pragma once
// Pure-logic fixed-size ring buffer of short log lines, for the on-screen
// Truma/Victron activity log on the T3's OLED display. No Arduino
// dependency -- host-testable like the rest of this project's protocol
// logic, even though its only real consumer (TrumaLogDisplay) is
// Arduino-specific.
//
// Lines are truncated to MAX_LINE_LEN-1 chars (+ null terminator) to fit
// the OLED's 128px width at text size 1 (~21 chars at 6px/char).

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace trumalog {

constexpr size_t MAX_LINE_LEN = 22; // 21 visible chars + null terminator
constexpr size_t MAX_LINES = 16;    // more than fits on screen at once;
                                     // display only shows the most recent
                                     // N that fit (e.g. 7), scrolling as
                                     // new lines arrive -- keeping extra
                                     // history here costs little RAM and
                                     // means nothing is lost if the screen
                                     // can't show it all at once.

class LogRingBuffer {
public:
    // Appends a new line, truncating if too long. If the buffer is full,
    // the oldest line is discarded.
    void push_line(const char *text);

    // Number of lines currently stored (0..MAX_LINES).
    size_t count() const { return count_; }

    // Returns the i-th line in insertion order (0 = oldest still stored,
    // count()-1 = most recent). Returns "" (empty string) if i is out of
    // range rather than crashing -- callers iterating 0..count()-1 are
    // always safe, but this guards against off-by-one mistakes too.
    const char* get(size_t i) const;

private:
    char lines_[MAX_LINES][MAX_LINE_LEN] = {{0}};
    size_t head_ = 0;   // index where the NEXT line will be written
    size_t count_ = 0;  // number of valid lines (caps at MAX_LINES)
};

} // namespace trumalog
