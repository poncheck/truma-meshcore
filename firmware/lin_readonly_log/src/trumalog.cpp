#include "trumalog.hpp"

namespace trumalog {

void LogRingBuffer::push_line(const char *text) {
    char *dest = lines_[head_];
    size_t len = strlen(text);
    if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
    memcpy(dest, text, len);
    dest[len] = 0;

    head_ = (head_ + 1) % MAX_LINES;
    if (count_ < MAX_LINES) count_++;
}

const char* LogRingBuffer::get(size_t i) const {
    if (i >= count_) return "";
    // Oldest stored line is at (head_ - count_) mod MAX_LINES, wrapping
    // correctly whether or not the buffer has filled up yet.
    size_t start = (head_ + MAX_LINES - count_) % MAX_LINES;
    size_t idx = (start + i) % MAX_LINES;
    return lines_[idx];
}

} // namespace trumalog
