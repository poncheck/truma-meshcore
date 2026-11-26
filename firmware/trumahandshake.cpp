#include "trumahandshake.hpp"
#include <cstring>

namespace trumahandshake {

static bool identifier_matches_at(const uint8_t *payload, size_t len, size_t offset, size_t compare_len) {
    if (len < offset + compare_len) return false;
    return memcmp(&payload[offset], &IDENTIFIER[4 - compare_len], compare_len) == 0;
}

void Protocol::get_pid18_reply(uint8_t out[8]) const {
    // Port of inetbox.py answer_to_d8_message: FF if we have an update
    // queued for CP Plus, else FE (meaning "waiting for updates FROM you").
    out[0] = pending_write_.has_value() ? 0xFF : 0xFE;
    for (int i = 1; i < 8; i++) out[i] = 0xFF;
}

void Protocol::queue_write(const WriteIntent &intent) {
    // Start from the last known real status (safe "everything off" default
    // if nothing has ever been received -- see header comment for why).
    trumastatus::MainStatus merged;
    if (last_status_.has_value()) {
        merged = *last_status_;
    }

    // Overlay only the fields the caller actually asked to change. This is
    // the merge that matches inetbox.py's `{**self.status, **self.updates_to_send}`
    // -- without it, writing one field would silently wipe the others on
    // the real heater (verified the hard way: an earlier version of this
    // function sent the fields as-is with no merge, which the test suite
    // caught before this ever touched real hardware).
    if (intent.target_temp_room_celsius.has_value()) {
        merged.target_temp_room_celsius = intent.target_temp_room_celsius;
    }
    if (intent.heating_mode.has_value()) {
        merged.heating_mode = *intent.heating_mode;
    }
    if (intent.target_temp_water_celsius.has_value()) {
        merged.target_temp_water_celsius = intent.target_temp_water_celsius;
    }
    if (intent.el_power_level.has_value()) {
        merged.el_power_level = *intent.el_power_level;
    }
    if (intent.energy_mix.has_value()) {
        merged.energy_mix = *intent.energy_mix;
    }

    pending_write_ = merged;
}

void Protocol::on_master_frame(const trumalin::TransportFrameHeader &header, ResponseQueue &queue) {
    using trumalin::FrameType;

    if (header.type == FrameType::Single && header.sid == 0xB9 &&
        identifier_matches_at(header.payload.data(), header.payload.size(), 0, 2)) {
        // Heartbeat request -- reply with NODE_ADDRESS, len=2, sid=0xF9, payload=0x00
        uint8_t reply[] = {NODE_ADDRESS, 0x02, 0xF9, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
        queue.push(reply, sizeof(reply));
        return;
    }

    if (header.type == FrameType::Single && header.sid == 0xB0 &&
        header.payload.size() >= 5 &&
        memcmp(header.payload.data(), IDENTIFIER, 4) == 0) {
        // Assign-NAD request. We only support being assigned OUR OWN fixed
        // address (0x03) -- per inetbox.py, any other requested address is
        // a protocol case this slave role deliberately doesn't implement.
        if (header.payload[4] == NODE_ADDRESS) {
            uint8_t reply[] = {NODE_ADDRESS, 0x01, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            queue.push(reply, sizeof(reply));
        }
        // else: silently ignore -- CP Plus trying to assign a different NAD
        // than the one this firmware is hardcoded to use is not handled.
        return;
    }

    if ((header.type == FrameType::First) && (header.sid == 0xBA || header.sid == 0xBB) &&
        header.payload.size() >= 2 &&
        memcmp(header.payload.data(), &IDENTIFIER[2], 2) == 0) {
        // Start of a multi-frame upload/download. expected_bytes counts
        // from right after the 2-byte identifier tail, matching inetbox.py's
        // `expected_bytes - 2` adjustment.
        reassembly_sid_ = header.sid;
        reassembly_len_ = 0;
        reassembly_expected_ = (header.expected_bytes > 2) ? (size_t)(header.expected_bytes - 2) : 0;
        size_t copy_len = header.payload.size() - 2;
        if (copy_len > sizeof(reassembly_buf_)) copy_len = sizeof(reassembly_buf_);
        // Guard against indexing header.payload[2] when payload.size()==2
        // (copy_len==0, nothing to copy): std::vector::operator[] at an
        // out-of-range index is undefined behaviour even if the resulting
        // pointer is never dereferenced, so skip the memcpy call entirely
        // rather than relying on copy_len==0 to make it a no-op.
        if (copy_len > 0) {
            memcpy(reassembly_buf_, &header.payload[2], copy_len);
        }
        reassembly_len_ = copy_len;
        return;
    }

    if (header.type == FrameType::Consecutive && reassembly_sid_.has_value()) {
        size_t remaining = sizeof(reassembly_buf_) - reassembly_len_;
        size_t copy_len = header.payload.size();
        if (copy_len > remaining) copy_len = remaining;
        memcpy(&reassembly_buf_[reassembly_len_], header.payload.data(), copy_len);
        reassembly_len_ += copy_len;

        if (reassembly_len_ >= reassembly_expected_) {
            complete_transportlayer_request(*reassembly_sid_, reassembly_buf_, reassembly_len_, queue);
            reassembly_sid_.reset();
            reassembly_len_ = 0;
        }
        return;
    }

    // Anything else: not a message this slave role answers (matches
    // inetbox.py's silent "No idea how to answer this message" fallthrough).
}

void Protocol::complete_transportlayer_request(uint8_t sid, const uint8_t *payload, size_t len, ResponseQueue &queue) {
    if (sid == 0xBB) {
        // CP Plus is sending US a status buffer (a "download" from its
        // perspective). Ack first, per inetbox.py.
        uint8_t ack[] = {NODE_ADDRESS, 0x01, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        queue.push(ack, sizeof(ack));

        // Validate preamble + header, then hand the actual command payload
        // to trumastatus. Layout (per inetbox.py process_status_buffer_update):
        //   [0:10)  10-byte STATUS_BUFFER_PREAMBLE
        //   [10]    command_len
        //   [11]    command_id
        //   [12]    command_counter
        //   [13]    checksum (not re-validated here -- already covered by
        //           the LIN frame-level checksum in trumalin; the
        //           reference itself has a "todo: check the checksum" on
        //           this inner one too)
        //   [14:]   command-specific payload
        if (len >= 14 && memcmp(payload, trumastatus::STATUS_BUFFER_PREAMBLE, 10) == 0) {
            uint8_t command_id = payload[11];
            if (command_id == trumastatus::COMMAND_ID_STATUS && len >= 14 + 20) {
                trumastatus::MainStatus status;
                if (trumastatus::parse_main_status(&payload[14], len - 14, &status)) {
                    last_status_ = status;
                }
            }
            // command_id == COMMAND_ID_COMMAND_COUNTER (0x0D) and other
            // buffer types: intentionally not parsed yet (timer/clock,
            // Phase 4 per the project plan) -- silently ignored, matching
            // inetbox.py's "Unknown status buffer type" warning-and-skip.
        }
    } else if (sid == 0xBA) {
        // CP Plus is asking US to upload (send) our pending write, if any.
        if (!pending_write_.has_value()) {
            return; // inetbox.py: "Not responding, waiting for status message first!"
        }

        uint8_t write_payload[12];
        size_t write_len = trumastatus::serialize_main_status_write(*pending_write_, write_payload, sizeof(write_payload));

        // Increment the command counter BEFORE building the buffer, per
        // inetbox.py's _get_status_buffer_for_writing:
        //   self.status["_command_counter"] = (self.status["_command_counter"] + 1) % 0xFF
        // Verified against the real reference implementation (not guessed):
        // running it on a synthetic receive+write produced command_counter=6
        // (started at 5) and checksum=0x86 for a known byte sequence --
        // see trumahandshake test suite for the exact reproduction.
        command_counter_ = (uint8_t)((command_counter_ + 1) % 0xFF);

        // Build the 10-preamble + 4-header + write_payload send buffer,
        // padded to 38 bytes total per inetbox.py's `send_buffer += bytes(38 - len(send_buffer))`.
        uint8_t send_buffer[38] = {0};
        memcpy(send_buffer, trumastatus::STATUS_BUFFER_PREAMBLE, 10);
        send_buffer[10] = (uint8_t)write_len;                      // command_len
        send_buffer[11] = trumastatus::COMMAND_ID_STATUS_WRITE;    // command_id (0x32, write variant)
        send_buffer[12] = command_counter_;                        // command_counter
        // Checksum covers STATUS_BUFFER_PREAMBLE[8:10] + [write_len, cid_write,
        // counter] + write_payload -- per inetbox.py's calculate_checksum call
        // in _get_status_buffer_for_writing (STATUS_HEADER_CHECKSUM_START = 8).
        uint8_t checksum_input[2 + 3 + 12];
        size_t ci_len = 0;
        checksum_input[ci_len++] = trumastatus::STATUS_BUFFER_PREAMBLE[8];
        checksum_input[ci_len++] = trumastatus::STATUS_BUFFER_PREAMBLE[9];
        checksum_input[ci_len++] = (uint8_t)write_len;
        checksum_input[ci_len++] = trumastatus::COMMAND_ID_STATUS_WRITE;
        checksum_input[ci_len++] = command_counter_;
        memcpy(&checksum_input[ci_len], write_payload, write_len);
        ci_len += write_len;
        send_buffer[13] = trumalin::calculate_checksum(checksum_input, ci_len);

        memcpy(&send_buffer[14], write_payload, write_len);

        // Chunk into the fixed 6-frame transport-layer upload response,
        // byte-for-byte matching inetbox.py's _get_status_buffer_for_writing framing.
        uint8_t header_frame[] = {NODE_ADDRESS, 0x10, 0x29, 0xFA, 0x00, 0x1F, 0x00, 0x1E};
        queue.push(header_frame, sizeof(header_frame));

        uint8_t chunk[8];
        for (int i = 0; i < 6; i++) {
            chunk[0] = NODE_ADDRESS;
            chunk[1] = (uint8_t)(0x21 + i);
            memcpy(&chunk[2], &send_buffer[2 + i * 6], 6);
            queue.push(chunk, sizeof(chunk));
        }

        pending_write_.reset();
    }
}

} // namespace trumahandshake
