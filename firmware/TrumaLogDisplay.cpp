#include "TrumaLogDisplay.h"

// OLED is 128x64. At text size 1 (default 6x8px glyphs with Adafruit_GFX),
// that's 8 rows of 8px each. Reserve the top row for a static header
// ("TRUMA/VICTRON LOG"), leaving 7 rows for scrolling log lines.
static constexpr int ROW_HEIGHT_PX = 8;
static constexpr int HEADER_ROWS = 1;

void TrumaLogDisplay::render() {
    if (!_display->isOn()) return;

    int visible_rows = (_display->height() / ROW_HEIGHT_PX) - HEADER_ROWS;
    if (visible_rows < 1) visible_rows = 1;

    _display->startFrame();

    _display->setTextSize(1);
    _display->setColor(UIColor::title_txt);
    _display->setCursor(0, 0);
    _display->print("TRUMA/VICTRON LOG");

    size_t total = _log->count();
    size_t show = (total < (size_t)visible_rows) ? total : (size_t)visible_rows;
    size_t start = total - show; // skip older lines that don't fit

    _display->setColor(UIColor::primary_txt);
    for (size_t i = 0; i < show; i++) {
        _display->setCursor(0, (int)(HEADER_ROWS + i) * ROW_HEIGHT_PX);
        _display->print(_log->get(start + i));
    }

    _display->endFrame();
}
