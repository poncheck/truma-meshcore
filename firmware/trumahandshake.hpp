#pragma once
// Port of inetbox.py's InetboxLINProtocol + Lin transport-layer state
// machine (lin.py's parse_transportlayer_master2slave /
// parse_transportlayer_slave2master / prepare_transportlayer_response /
// _answer_transportlayer_request), restricted to exactly what our iNet-Box
// slave role needs to answer:
//
//   SID 0xB9 (heartbeat)         -> single 0x00 byte reply
//   SID 0xB0 (assign NAD)        -> empty-payload ack
//   SID 0xBA (CP Plus requests upload FROM us) -> our status buffer, as a
//            multi-frame transport-layer response (header + up to 6 chunks)
//   SID 0xBB (CP Plus sends us a status download) -> ack + hands the raw
//            status-buffer bytes to trumastatus::parse_main_status
//   PID 0x18 ("D8 message")      -> FE/FF flag byte (FF = "I have an update
//            ready for you", gates whether CP Plus even bothers asking 0xBA)
//
// This class is deliberately transport-agnostic: it consumes already
// PID/checksum-validated transport-layer frame bytes (see trumalin.hpp for
// that lower layer) and produces reply frames into a caller-provided
// response queue. No UART/Serial calls anywhere in this file -- that keeps
// it host-testable with injected byte sequences, same pattern as
// trumalin/trumastatus.

#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include "trumalin.hpp"
#include "trumastatus.hpp"

namespace trumahandshake {

constexpr uint8_t NODE_ADDRESS = 0x03;
// The 4-byte "IDENTIFIER" inetbox.py uses to recognise messages addressed
// to an iNet-Box-role device (not a NAD/address -- a fixed protocol
// constant every real Truma iNet Box also uses, per the reference).
constexpr uint8_t IDENTIFIER[4] = {0x17, 0x46, 0x00, 0x1F};

constexpr size_t MAX_RESPONSE_FRAMES = 8; // single-ack cases need 1; upload (0xBA) needs up to 7
constexpr size_t FRAME_SIZE = 8;

struct ResponseQueue {
    std::array<std::array<uint8_t, FRAME_SIZE>, MAX_RESPONSE_FRAMES> frames{};
    size_t count = 0;

    bool push(const uint8_t *data, size_t len) {
        if (count >= MAX_RESPONSE_FRAMES || len > FRAME_SIZE) return false;
        auto &f = frames[count];
        f.fill(0xFF); // unused trailing bytes padded with 0xFF, matching the reference's style
        for (size_t i = 0; i < len; i++) f[i] = data[i];
        count++;
        return true;
    }
    bool empty() const { return count == 0; }
    // Pops the oldest queued frame into `out` (8 bytes). Returns false if empty.
    bool pop(uint8_t out[FRAME_SIZE]) {
        if (count == 0) return false;
        for (size_t i = 0; i < FRAME_SIZE; i++) out[i] = frames[0][i];
        for (size_t i = 1; i < count; i++) frames[i - 1] = frames[i];
        count--;
        return true;
    }
};

class Protocol {
public:
    // Call for every received transport-layer frame (PID 0x3C, master->slave)
    // once its header has already been parsed by trumalin::parse_transportlayer_frame_header.
    // `queue` receives any reply frame(s) to send back on PID 0x3D.
    void on_master_frame(const trumalin::TransportFrameHeader &header, ResponseQueue &queue);

    // Call when the CP Plus asks (PID 0x18) whether we have an update ready.
    // Returns the 8-byte reply payload (FE = "waiting", FF = "I have data for you").
    void get_pid18_reply(uint8_t out[8]) const;

    // Queue a write: the next time CP Plus asks for an upload (SID 0xBA),
    // this status will be sent instead of just acking. Pass std::nullopt to
    // clear a pending write (e.g. after confirming it was sent).
    void queue_write(const trumastatus::MainStatus &status);
    bool has_pending_write() const { return pending_write_.has_value(); }

    // The most recent status received FROM the CP Plus (via SID 0xBB), if any.
    const std::optional<trumastatus::MainStatus>& last_received_status() const { return last_status_; }

private:
    // Multi-frame reassembly state for an in-progress "first"+"consecutive" sequence.
    std::optional<uint8_t> reassembly_sid_;
    uint8_t reassembly_buf_[64] = {0};
    size_t reassembly_len_ = 0;
    size_t reassembly_expected_ = 0;

    std::optional<trumastatus::MainStatus> pending_write_;
    std::optional<trumastatus::MainStatus> last_status_;

    void complete_transportlayer_request(uint8_t sid, const uint8_t *payload, size_t len, ResponseQueue &queue);
};

} // namespace trumahandshake
