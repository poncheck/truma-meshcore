#pragma once
// Pure, host-portable (no Arduino/ESP32 dependency) LIN bus primitives for the
// Truma TIN-bus protocol. Ported from danielfett/inetbox.py (lin.py) and
// cross-checked against olivluca/TruMinus (trumaframes.cpp).
//
// This file is deliberately framework-free so it can be unit-tested on the
// host with plain g++ before ever touching real hardware or a live heater.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <optional>

namespace trumalin {

// Classic LIN checksum: inverted 8-bit sum with carry over the given bytes.
// Port of inetbox.py tools.calculate_checksum (exact same wrap-at-0xFF
// behaviour, including the 0xFF -> 0x00 special case).
uint8_t calculate_checksum(const uint8_t *data, size_t len);

// Checks the parity bits of a LIN protected identifier byte and returns the
// plain 6-bit PID. Throws std::runtime_error (via exception) on parity
// mismatch -- caller should catch and discard the frame, exactly like
// inetbox.py's Lin.check_pid_parity.
uint8_t check_pid_parity(uint8_t protected_id);

// LIN transport-layer frame types, per ISO 17987 / LIN 2.x transport layer
// (identical framing used by inetbox.py's parse_transportlayer_frame_header).
enum class FrameType { Single, First, Consecutive, Reserved };

struct TransportFrameHeader {
    uint8_t node_address;
    FrameType type;
    int expected_bytes = -1; // only meaningful for Single/First
    uint8_t sid = 0;         // only meaningful for Single/First
    std::vector<uint8_t> payload;
};

// Parses an 8-byte LIN transport-layer frame body (i.e. frame identifier
// already stripped) into node address / frame type / sid / payload, mirroring
// inetbox.py's parse_transportlayer_frame_header byte-for-byte.
TransportFrameHeader parse_transportlayer_frame_header(const uint8_t *databytes, size_t len);

constexpr uint8_t PID_TRANSPORTLAYER_MASTER2SLAVE = 0x3C;
constexpr uint8_t PID_TRANSPORTLAYER_SLAVE2MASTER = 0x3D;
constexpr uint8_t NODE_ADDRESS_BROADCAST = 0x7F;

} // namespace trumalin
