#pragma once
// Byte-stream LIN frame synchronizer: turns a raw stream of bytes arriving
// from the LIN-UART transceiver into complete, checksum-and-parity-
// validated LIN frames (6-bit PID + 8 data bytes). This is the missing
// "glue" layer between the physical UART and the already-tested
// trumalin::parse_transportlayer_frame_header -- that function operates on
// an already-extracted 8-byte frame body; this class is what extracts it
// from a continuous byte stream in the first place.
//
// Port of inetbox.py's Lin.loop_serial/_read_passive byte-sync logic:
//   break(0x00) -> sync(0x55) -> protected_pid -> 8 data bytes -> checksum
// Exactly matching the reference's synchronization and checksum rules
// (classic checksum -- pid excluded -- for PID 0x3C/0x3D; extended
// checksum -- pid included -- for every other PID).
//
// No UART/Serial dependency -- feed_byte() takes one byte at a time, so
// this is host-testable with an injected byte sequence, same pattern as
// the rest of this project's protocol logic.

#include <cstdint>
#include <cstddef>

namespace trumaframe {

struct LinFrame {
    uint8_t pid;       // 6-bit PID, parity bits already stripped/validated
    uint8_t data[8];
};

enum class FeedResult { Incomplete, PidDetected, FrameReady, ParityError, ChecksumError };

class FrameReader {
public:
    // Feed one byte from the UART. When a complete, valid frame has been
    // assembled, returns FrameReady and fills `out`. ParityError/
    // ChecksumError mean a frame was detected but rejected (matches
    // inetbox.py's behaviour of silently dropping and resyncing on the
    // next break+sync) -- `out` is not touched in those cases. Incomplete
    // means "keep feeding bytes".
    //
    // PidDetected fires exactly once per frame, immediately after the
    // protected PID byte passes its parity check -- BEFORE any of the 8
    // data bytes have arrived. `out->pid` is valid at this point (data[]
    // is not). This exists so a caller acting as a LIN slave can respond
    // to a transport-layer response slot (PID 0x3D) by writing bytes onto
    // the bus THEN AND THERE, instead of only ever being able to react
    // after a complete frame (by which point someone else has already
    // filled -- or the bus has gone recessive on -- those 8 data byte
    // slots). After PidDetected, feeding further bytes continues normally
    // and will still eventually yield FrameReady/ChecksumError/ParityError
    // for whatever ends up on the bus (ours or someone else's) -- this is
    // purely an earlier notification, not a change to the state machine's
    // existing data-collection behaviour.
    FeedResult feed_byte(uint8_t b, LinFrame *out);

private:
    enum class State { WaitBreak, WaitSync, WaitPid, ReadingData };
    State state_ = State::WaitBreak;
    uint8_t raw_pid_ = 0;
    uint8_t buf_[9] = {0}; // 8 data bytes + 1 checksum byte
    size_t buf_count_ = 0;
};

} // namespace trumaframe
