// Host-side tests for trumahandshake.cpp -- the LIN master<->slave
// handshake state machine (heartbeat, assign-NAD, multi-frame upload/
// download, command_counter increment, inner checksum computation, and
// the write-merge safety behaviour).
//
// The write-path test (counter + checksum) is verified against actual
// output from running the real inetbox.py reference implementation on an
// equivalent receive+write sequence -- see the exact reproduction script
// in project notes; expected values (counter=6, checksum=0x86) are not
// guessed, they are copy-pasted from that script's real output.

#include "../trumahandshake.hpp"
#include "../trumalin.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
    g_checks++; \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        g_failures++; \
        std::printf("FAIL [%s:%d] %s (got %d, expected %d)\n", __FILE__, __LINE__, msg, (int)_a, (int)_b); \
    } \
} while (0)

static trumalin::TransportFrameHeader make_single(uint8_t node_addr, uint8_t sid, const uint8_t *payload, size_t len) {
    trumalin::TransportFrameHeader h{};
    h.node_address = node_addr;
    h.type = trumalin::FrameType::Single;
    h.sid = sid;
    h.expected_bytes = (int)len;
    for (size_t i = 0; i < len; i++) h.payload.push_back(payload[i]);
    return h;
}

// Drives a full SID 0xBB (CP Plus -> us, status download) sequence with the
// given 20-byte COMMAND_STATUS payload (the same reference-verified bytes
// used throughout this project: room=21C, water_target=40C/eco, gas,
// water_current=38.5C, room_current=19.2C, status=5, error=0).
static void feed_status_download(trumahandshake::Protocol &proto, const uint8_t status_payload[20]) {
    uint8_t inner[34];
    memcpy(inner, trumastatus::STATUS_BUFFER_PREAMBLE, 10);
    inner[10] = 20;
    inner[11] = trumastatus::COMMAND_ID_STATUS;
    inner[12] = 7; // counter, arbitrary -- not inspected by this layer
    inner[13] = 0; // checksum, not validated by this layer (matches inetbox.py's own "todo")
    memcpy(&inner[14], status_payload, 20);

    trumalin::TransportFrameHeader first{};
    first.node_address = trumahandshake::NODE_ADDRESS;
    first.type = trumalin::FrameType::First;
    first.sid = 0xBB;
    first.expected_bytes = (int)(sizeof(inner) + 2);
    first.payload.push_back(0x00);
    first.payload.push_back(0x1F);
    for (size_t i = 0; i < 4; i++) first.payload.push_back(inner[i]);

    trumahandshake::ResponseQueue discard;
    proto.on_master_frame(first, discard);

    trumalin::TransportFrameHeader cont{};
    cont.type = trumalin::FrameType::Consecutive;
    for (size_t i = 4; i < sizeof(inner); i++) cont.payload.push_back(inner[i]);
    proto.on_master_frame(cont, discard);
}

// Drives a full SID 0xBA (CP Plus asks us to upload our pending write)
// sequence and returns the resulting reply frames via `queue`.
static void request_upload(trumahandshake::Protocol &proto, trumahandshake::ResponseQueue &queue) {
    trumalin::TransportFrameHeader req{};
    req.type = trumalin::FrameType::First;
    req.sid = 0xBA;
    req.expected_bytes = 2;
    req.payload.push_back(0x00);
    req.payload.push_back(0x1F);
    proto.on_master_frame(req, queue);

    trumalin::TransportFrameHeader cont{};
    cont.type = trumalin::FrameType::Consecutive;
    proto.on_master_frame(cont, queue);
}

void test_heartbeat_reply() {
    trumahandshake::Protocol proto;
    trumahandshake::ResponseQueue queue;

    uint8_t payload[] = {0x00, 0x1F};
    auto header = make_single(trumahandshake::NODE_ADDRESS, 0xB9, payload, sizeof(payload));

    proto.on_master_frame(header, queue);

    CHECK(!queue.empty(), "heartbeat produces a reply");
    uint8_t out[8];
    CHECK(queue.pop(out), "pop succeeds");
    uint8_t expected[] = {trumahandshake::NODE_ADDRESS, 0x02, 0xF9, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(memcmp(out, expected, 8) == 0, "heartbeat reply matches inetbox.py's exact byte layout");
}

void test_assign_nad_own_address() {
    trumahandshake::Protocol proto;
    trumahandshake::ResponseQueue queue;

    uint8_t payload[5] = {0x17, 0x46, 0x00, 0x1F, trumahandshake::NODE_ADDRESS};
    auto header = make_single(trumahandshake::NODE_ADDRESS, 0xB0, payload, sizeof(payload));

    proto.on_master_frame(header, queue);

    CHECK(!queue.empty(), "assign-NAD for our own address produces a reply");
    uint8_t out[8];
    queue.pop(out);
    uint8_t expected[] = {trumahandshake::NODE_ADDRESS, 0x01, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(memcmp(out, expected, 8) == 0, "assign-NAD reply matches inetbox.py's exact byte layout");
}

void test_assign_nad_other_address_ignored() {
    trumahandshake::Protocol proto;
    trumahandshake::ResponseQueue queue;

    uint8_t payload[5] = {0x17, 0x46, 0x00, 0x1F, 0x05};
    auto header = make_single(trumahandshake::NODE_ADDRESS, 0xB0, payload, sizeof(payload));

    proto.on_master_frame(header, queue);

    CHECK(queue.empty(), "assign-NAD for a different address is silently ignored (not implemented, per inetbox.py)");
}

void test_pid18_reply_no_pending_write() {
    trumahandshake::Protocol proto;
    uint8_t out[8];
    proto.get_pid18_reply(out);
    CHECK_EQ((int)out[0], 0xFE, "PID 0x18 reply is FE (waiting) when no write is queued");
}

void test_pid18_reply_with_pending_write() {
    trumahandshake::Protocol proto;
    trumahandshake::Protocol::WriteIntent intent;
    intent.target_temp_room_celsius = 21.0;
    proto.queue_write(intent);

    uint8_t out[8];
    proto.get_pid18_reply(out);
    CHECK_EQ((int)out[0], 0xFF, "PID 0x18 reply is FF (update ready) once a write is queued");
}

void test_receive_status_download_full_reassembly() {
    trumahandshake::Protocol proto;
    trumahandshake::ResponseQueue queue;

    uint8_t status_payload[20] = {
        0x7c, 0x0b, 0x01, 0x00, 0x84, 0x03, 0x3a, 0x0c, 0x84, 0x03,
        0x01, 0x01, 0x2b, 0x0c, 0x6a, 0x0b, 0x05, 0x00, 0x00, 0x00
    };

    uint8_t inner[34];
    memcpy(inner, trumastatus::STATUS_BUFFER_PREAMBLE, 10);
    inner[10] = 20;
    inner[11] = trumastatus::COMMAND_ID_STATUS;
    inner[12] = 7;
    inner[13] = 0;
    memcpy(&inner[14], status_payload, 20);

    trumalin::TransportFrameHeader first{};
    first.node_address = trumahandshake::NODE_ADDRESS;
    first.type = trumalin::FrameType::First;
    first.sid = 0xBB;
    first.expected_bytes = (int)(sizeof(inner) + 2);
    first.payload.push_back(0x00);
    first.payload.push_back(0x1F);
    for (size_t i = 0; i < 4; i++) first.payload.push_back(inner[i]);

    proto.on_master_frame(first, queue);
    CHECK(queue.empty(), "no reply yet mid-reassembly");

    trumalin::TransportFrameHeader cont{};
    cont.type = trumalin::FrameType::Consecutive;
    for (size_t i = 4; i < sizeof(inner); i++) cont.payload.push_back(inner[i]);

    proto.on_master_frame(cont, queue);

    CHECK(!queue.empty(), "ack is queued once reassembly completes");
    uint8_t ack[8];
    queue.pop(ack);
    uint8_t expected_ack[] = {trumahandshake::NODE_ADDRESS, 0x01, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(memcmp(ack, expected_ack, 8) == 0, "download ack matches inetbox.py's exact byte layout");

    auto &last = proto.last_received_status();
    CHECK(last.has_value(), "status was parsed and stored");
    if (last.has_value()) {
        CHECK(last->target_temp_room_celsius.has_value(), "target_temp_room parsed");
        if (last->target_temp_room_celsius.has_value()) {
            CHECK(*last->target_temp_room_celsius > 20.9 && *last->target_temp_room_celsius < 21.1,
                  "target_temp_room == ~21.0C");
        }
    }
}

void test_write_merges_unset_fields_from_last_status() {
    // SAFETY TEST: this is the exact bug caught before touching real
    // hardware -- queue_write() with a sparse WriteIntent (only room temp
    // set) must NOT wipe the other fields (water target, energy mix) that
    // were last reported by a real CP Plus. Verifies the merge, not just
    // that *a* write happens.
    trumahandshake::Protocol proto;

    uint8_t status_payload[20] = {
        0x7c, 0x0b, 0x01, 0x00, 0x84, 0x03, 0x3a, 0x0c, 0x84, 0x03,
        0x01, 0x01, 0x2b, 0x0c, 0x6a, 0x0b, 0x05, 0x00, 0x00, 0x00
    }; // target_temp_water=40C(eco), energy_mix=1(gas), el_power_level=900
    feed_status_download(proto, status_payload);

    CHECK(proto.last_received_status().has_value(), "precondition: status was received");

    // Sparse intent: ONLY change the room temperature, nothing else.
    trumahandshake::Protocol::WriteIntent intent;
    intent.target_temp_room_celsius = 22.5;
    proto.queue_write(intent);

    trumahandshake::ResponseQueue queue;
    request_upload(proto, queue);

    CHECK(!queue.empty(), "write produced upload frames");
    uint8_t header_frame[8], chunk0[8], chunk1[8], chunk2[8], chunk3[8];
    queue.pop(header_frame);
    queue.pop(chunk0); // send_buffer[2:8)  -- preamble bytes, not write_payload
    queue.pop(chunk1); // send_buffer[8:14) -- preamble tail + len/cid/counter/checksum
    queue.pop(chunk2); // send_buffer[14:20) -- write_payload[0:6): room_temp(2) + heating_mode(1) + reserved(1) + el_power_level(2)
    queue.pop(chunk3); // send_buffer[20:26) -- write_payload[6:12): water_target(2) + el_power_level_dup(2) + energy_mix(1) + energy_mix_dup(1)

    // chunk2 local layout: [0]=NODE_ADDR [1]=sub-id [2:4]=room_temp_code [4]=heating_mode [5]=reserved [6:8]=el_power_level
    uint16_t written_room_code = (uint16_t)(chunk2[2] | (chunk2[3] << 8));
    double written_room_c = written_room_code / 10.0 - 273.0;
    CHECK(written_room_c > 22.4 && written_room_c < 22.6, "the field we DID set (room temp) was written correctly");

    uint8_t written_heating_mode = chunk2[4];
    CHECK_EQ((int)written_heating_mode, (int)trumastatus::HeatingMode::Eco,
             "heating_mode we did NOT set was carried over from last received status (eco), not reset to Off");

    uint16_t written_el_power = (uint16_t)(chunk2[6] | (chunk2[7] << 8));
    CHECK_EQ((int)written_el_power, 900, "el_power_level we did NOT set was carried over (900), not reset to 0");

    // chunk3 local layout: [0]=NODE_ADDR [1]=sub-id [2:4]=water_target_code [4:6]=el_power_level_dup [6]=energy_mix [7]=energy_mix_dup
    uint16_t written_water_code = (uint16_t)(chunk3[2] | (chunk3[3] << 8));
    double written_water_c = written_water_code / 10.0 - 273.0;
    CHECK(written_water_c > 39.9 && written_water_c < 40.1,
          "target_temp_water we did NOT set was carried over (40C/eco), not reset to off");

    uint8_t written_energy_mix = chunk3[6];
    CHECK_EQ((int)written_energy_mix, 1, "energy_mix we did NOT set was carried over (1=gas), not reset to 0");
}

void test_write_increments_counter_and_computes_checksum() {
    // Reproduces, byte-for-byte, the Python verification:
    //   app.status['_command_counter'] = 5
    //   app.process_status_buffer_update(<same status buffer as above>)
    //   app.set_status('target_temp_room', '21'); app.set_status('heating_mode', 'eco')
    //   buf = app._get_status_buffer_for_writing()
    //   -> buf[12] (counter) == 6, buf[13] (checksum) == 0x86
    trumahandshake::Protocol proto;

    uint8_t status_payload[20] = {
        0x7c, 0x0b, 0x01, 0x00, 0x84, 0x03, 0x3a, 0x0c, 0x84, 0x03,
        0x01, 0x01, 0x2b, 0x0c, 0x6a, 0x0b, 0x05, 0x00, 0x00, 0x00
    };
    feed_status_download(proto, status_payload);

    // Bump command_counter_ to 5 via 5 dummy write cycles (no direct setter
    // exposed on purpose -- the counter must only ever change via the real
    // write path, same as inetbox.py never lets you set it directly either
    // except via its own internal increment).
    trumahandshake::Protocol::WriteIntent dummy;
    dummy.target_temp_room_celsius = 10.0;
    for (int i = 0; i < 5; i++) {
        proto.queue_write(dummy);
        trumahandshake::ResponseQueue discard;
        request_upload(proto, discard);
    }
    CHECK_EQ((int)proto.command_counter(), 5, "counter reached 5 after 5 write cycles");

    // The real test write: room=21, heating=eco (water_target/energy_mix
    // carried over from the fed status, exactly matching the Python
    // reproduction which also received that same status first).
    trumahandshake::Protocol::WriteIntent intent;
    intent.target_temp_room_celsius = 21.0;
    intent.heating_mode = trumastatus::HeatingMode::Eco;
    proto.queue_write(intent);

    trumahandshake::ResponseQueue queue;
    request_upload(proto, queue);

    CHECK_EQ((int)proto.command_counter(), 6, "counter incremented to 6 for this write (matches Python reproduction)");

    CHECK(!queue.empty(), "upload response frames queued");
    uint8_t header_frame[8];
    queue.pop(header_frame);
    uint8_t expected_header[] = {trumahandshake::NODE_ADDRESS, 0x10, 0x29, 0xFA, 0x00, 0x1F, 0x00, 0x1E};
    CHECK(memcmp(header_frame, expected_header, 8) == 0, "upload header frame matches inetbox.py's layout");

    uint8_t chunk0[8];
    queue.pop(chunk0);
    CHECK_EQ((int)chunk0[0], trumahandshake::NODE_ADDRESS, "chunk0 node address");
    CHECK_EQ((int)chunk0[1], 0x21, "chunk0 sub-id");

    uint8_t chunk1[8];
    queue.pop(chunk1);
    CHECK_EQ((int)chunk1[1], 0x22, "chunk1 sub-id");
    CHECK_EQ((int)chunk1[2 + 4], 6, "command_counter byte in the wire-format chunk matches counter()==6");
    CHECK_EQ((int)chunk1[2 + 5], 0x86, "checksum byte in the wire-format chunk matches the Python-verified 0x86");
}

int main() {
    test_heartbeat_reply();
    test_assign_nad_own_address();
    test_assign_nad_other_address_ignored();
    test_pid18_reply_no_pending_write();
    test_pid18_reply_with_pending_write();
    test_receive_status_download_full_reassembly();
    test_write_merges_unset_fields_from_last_status();
    test_write_increments_counter_and_computes_checksum();

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
