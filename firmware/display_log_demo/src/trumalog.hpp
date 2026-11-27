#pragma once
// Pure-logic fixed-size ring buffer of short log lines, for the on-screen
// Truma/Victron activity log on the T3's OLED display. No Arduino
// dependency -- host-testable like the rest of this project's protocol
// logic, even though its only real consumer (TrumaLogDisplay) is
// Arduino-specific.
//
// NOTE: this is a build-time COPY of ~/.hermes/apps/truma-meshcore/firmware/trumalog.hpp
// for this standalone demo project (display_log_demo), which cannot easily
// reference files outside its own src/ directory via PlatformIO's default
// source filter. The canonical, host-tested version lives in firmware/ --
// this demo project is throwaway (same pattern as lin_probe/uart_probe
// earlier in this project) and will be deleted after tonight's test.
//
// Lines are truncated to MAX_LINE_LEN-1 chars (+ null terminator) to fit
// the OLED's 128px width at text size 1 (~21 chars at 6px/char).

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace trumalog {

constexpr size_t MAX_LINE_LEN = 22;
constexpr size_t MAX_LINES = 16;

class LogRingBuffer {
public:
    void push_line(const char *text);
    size_t count() const { return count_; }
    const char* get(size_t i) const;

private:
    char lines_[MAX_LINES][MAX_LINE_LEN] = {{0}};
    size_t head_ = 0;
    size_t count_ = 0;
};

} // namespace trumalog
