#pragma once
// Builds the raw byte sequence a LIN slave writes onto the bus in answer
// to a master's request -- the send-side counterpart to trumaframereader's
// receive-side parsing. Port of inetbox.py's Lin._send_answer:
//   - PID 0x3C/0x3D (transport layer): classic checksum (pid excluded)
//   - any other PID (e.g. 0x18 heartbeat-style ANSWER_TO_PIDS): extended
//     checksum (pid included)
//
// No UART/Serial dependency -- build_answer_bytes() returns the exact
// bytes to write (8 data bytes + 1 checksum byte -- the PID itself is
// NOT included, since on a real LIN bus the slave only ever writes the
// data+checksum portion; the PID byte on the wire was already driven by
// the master and is not re-transmitted by the slave).

#include <cstdint>
#include <cstddef>

namespace trumalinwriter {

// `payload` must be exactly 8 bytes (a LIN data field is always 8 bytes).
// `out` must have capacity >= 9 (8 data bytes + 1 checksum byte). Returns
// 9 (always, for a well-formed call) or 0 if `out_capacity` is too small.
//
// `raw_pid` is the PID byte AS RECEIVED FROM THE BUS (protected, with
// parity bits) -- required for the extended-checksum case. For PID 0x3C/
// 0x3D it is unused (classic checksum excludes it) but still accepted for
// a uniform call signature.
size_t build_answer_bytes(uint8_t pid, uint8_t raw_pid, const uint8_t payload[8],
                            uint8_t *out, size_t out_capacity);

} // namespace trumalinwriter
