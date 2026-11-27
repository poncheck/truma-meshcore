#include "trumalin.hpp"
#include <stdexcept>

namespace trumalin {

uint8_t calculate_checksum(const uint8_t *data, size_t len) {
    // Port of inetbox.py tools.calculate_checksum:
    //   cs = 0
    //   for b in bytestring: cs = (cs + b) % 0xFF
    //   cs = ~cs & 0xFF
    //   if cs == 0xFF: cs = 0
    unsigned int cs = 0;
    for (size_t i = 0; i < len; ++i) {
        cs = (cs + data[i]) % 0xFF;
    }
    cs = (~cs) & 0xFF;
    if (cs == 0xFF) {
        cs = 0;
    }
    return static_cast<uint8_t>(cs);
}

uint8_t check_pid_parity(uint8_t byte) {
    // Port of inetbox.py Lin.check_pid_parity. Bit numbering: ID0 is the
    // LSB of the lower six bits, P0/P1 are bits 6/7.
    //   P0 = ID0 ^ ID1 ^ ID2 ^ ID4
    //   P1 = !(ID1 ^ ID3 ^ ID4 ^ ID5)
    int id0 = byte & 0x01;
    int id1 = (byte & 0x02) >> 1;
    int id2 = (byte & 0x04) >> 2;
    int id3 = (byte & 0x08) >> 3;
    int id4 = (byte & 0x10) >> 4;
    int id5 = (byte & 0x20) >> 5;

    int p0 = id0 ^ id1 ^ id2 ^ id4;
    int p1 = !(id1 ^ id3 ^ id4 ^ id5);

    int recv_p0 = (byte & 0x40) >> 6;
    int recv_p1 = (byte & 0x80) >> 7;

    if (!(p0 == recv_p0 && p1 == recv_p1)) {
        throw std::runtime_error("LIN PID parity error");
    }
    return byte & 0x3F;
}

TransportFrameHeader parse_transportlayer_frame_header(const uint8_t *databytes, size_t len) {
    TransportFrameHeader h{};
    if (len < 2) {
        h.type = FrameType::Reserved;
        return h;
    }
    h.node_address = databytes[0];

    // Upper 4 bits of the PCI byte (databytes[1]) select the frame type,
    // exactly as in inetbox.py.
    uint8_t pci_identifier = databytes[1] >> 4;

    if (pci_identifier == 0x0) {
        h.type = FrameType::Single;
        h.expected_bytes = (databytes[1] & 0x0F) - 1;
        if (len > 2) h.sid = databytes[2];
        for (size_t i = 3; i < len && static_cast<int>(i - 3) < h.expected_bytes; ++i) {
            h.payload.push_back(databytes[i]);
        }
    } else if (pci_identifier == 0x1) {
        h.type = FrameType::First;
        h.expected_bytes = ((databytes[1] & 0x0F) << 8 | (len > 2 ? databytes[2] : 0)) - 1;
        if (len > 3) h.sid = databytes[3];
        for (size_t i = 4; i < len; ++i) {
            h.payload.push_back(databytes[i]);
        }
    } else if (pci_identifier == 0x2) {
        h.type = FrameType::Consecutive;
        for (size_t i = 2; i < len; ++i) {
            h.payload.push_back(databytes[i]);
        }
    } else {
        h.type = FrameType::Reserved;
    }
    return h;
}

} // namespace trumalin
