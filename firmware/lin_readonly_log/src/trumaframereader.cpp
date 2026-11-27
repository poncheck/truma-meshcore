#include "trumaframereader.hpp"
#include "trumalin.hpp"
#include <cstring>

namespace trumaframe {

FeedResult FrameReader::feed_byte(uint8_t b, LinFrame *out) {
    switch (state_) {
        case State::WaitBreak:
            if (b == 0x00) {
                state_ = State::WaitSync;
            }
            // else: stay in WaitBreak (matches inetbox.py silently
            // discarding bytes until a 0x00 break appears).
            return FeedResult::Incomplete;

        case State::WaitSync:
            if (b == 0x55) {
                state_ = State::WaitPid;
            } else {
                // Not a valid break+sync pair -- resync: this byte might
                // itself be the start of a new break.
                state_ = (b == 0x00) ? State::WaitSync : State::WaitBreak;
            }
            return FeedResult::Incomplete;

        case State::WaitPid: {
            raw_pid_ = b;
            try {
                trumalin::check_pid_parity(b); // throws on parity failure
            } catch (...) {
                state_ = State::WaitBreak;
                return FeedResult::ParityError;
            }
            state_ = State::ReadingData;
            buf_count_ = 0;
            out->pid = raw_pid_ & 0x3F; // valid immediately, data[] is not yet
            return FeedResult::PidDetected;
        }

        case State::ReadingData: {
            buf_[buf_count_++] = b;
            if (buf_count_ < 9) {
                return FeedResult::Incomplete;
            }
            // Have 8 data bytes + 1 checksum byte now.
            state_ = State::WaitBreak; // always resync after a complete frame attempt

            uint8_t pid = raw_pid_ & 0x3F;
            uint8_t received_checksum = buf_[8];
            uint8_t computed_checksum;

            if (pid == trumalin::PID_TRANSPORTLAYER_MASTER2SLAVE ||
                pid == trumalin::PID_TRANSPORTLAYER_SLAVE2MASTER) {
                // Classic checksum: covers only the 8 data bytes, NOT the pid.
                computed_checksum = trumalin::calculate_checksum(buf_, 8);
            } else {
                // Extended checksum: covers raw_pid + 8 data bytes.
                uint8_t with_pid[9];
                with_pid[0] = raw_pid_;
                memcpy(&with_pid[1], buf_, 8);
                computed_checksum = trumalin::calculate_checksum(with_pid, 9);
            }

            if (computed_checksum != received_checksum) {
                return FeedResult::ChecksumError;
            }

            out->pid = pid;
            memcpy(out->data, buf_, 8);
            return FeedResult::FrameReady;
        }
    }
    return FeedResult::Incomplete; // unreachable, silences -Wreturn-type
}

} // namespace trumaframe
