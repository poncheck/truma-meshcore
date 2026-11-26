#pragma once
// Renders trumalog::LogRingBuffer onto the T3's OLED (via MeshCore's
// DisplayDriver interface) as a scrolling activity log -- newest line at
// the bottom, scrolling up as new lines arrive, same convention as a
// typical terminal/serial monitor.
//
// This is deliberately a thin rendering layer: all the interesting logic
// (what to log, when) lives in main.cpp calling trumalog::LogRingBuffer::
// push_line() directly; this class only knows how to draw whatever is
// already in the buffer.

#include <helpers/ui/DisplayDriver.h>
#include "trumalog.hpp"

class TrumaLogDisplay {
public:
    TrumaLogDisplay(DisplayDriver &display, trumalog::LogRingBuffer &log)
        : _display(&display), _log(&log) {}

    // Call periodically (e.g. once per second) from loop(). Does nothing
    // if the display is off (same on/off/auto-off behavior as MeshCore's
    // own UITask -- this does NOT manage power state itself, call
    // _display->turnOn()/turnOff() from the caller as needed).
    void render();

private:
    DisplayDriver *_display;
    trumalog::LogRingBuffer *_log;
};
