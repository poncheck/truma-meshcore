#pragma once
// Port of inetbox.py's status-buffer layer (InetboxApp.process_status_buffer_update,
// TrumaCommand, STATUS_BUFFER_PREAMBLE) for command ID 0x33 ("main status" --
// the one carrying target/current room+water temp, heating mode, energy
// mix, operating status and error code -- everything the `truma status`,
// `truma temp`, `truma heating`, `truma boiler` CLI commands need).
//
// Byte layout verified against inetbox.py/inetbox.py's TrumaCommand(0x33, ...)
// bitstruct field list (COMMAND_STATUS), not guessed:
//   offset 0-1  (u16) target_temp_room       (temp code, see trumaconv)
//   offset 2    (u8)  heating_mode           (0=off,1=eco,10=boost)
//   offset 3    (u8)  _recv_status_u3        (unknown/reserved)
//   offset 4-5  (u16) el_power_level
//   offset 6-7  (u16) target_temp_water      (temp code)
//   offset 8-9  (u16) el_power_level (dup -- same field written twice in
//                      the reference's bitstruct field list; both offsets
//                      map to the same logical value)
//   offset 10   (u8)  energy_mix
//   offset 11   (u8)  energy_mix (dup, same note as el_power_level above)
//   -- read-only tail (only present on frames FROM the CP Plus, not when
//      writing status buffer updates TO it) --
//   offset 12-13 (u16) current_temp_water    (temp code)
//   offset 14-15 (u16) current_temp_room     (temp code)
//   offset 16    (u8)  operating_status
//   offset 17-18 (u16, raw bytes) error_code (byte[1]*100 + byte[0])
//   offset 19    (u8)  _recv_status_u10      (unknown/reserved)
//
// The 10-byte STATUS_BUFFER_PREAMBLE + 2-byte header (length, command id) +
// 1-byte command counter + 1-byte checksum always precede this payload on
// the wire; that framing is handled by trumahandshake, not here. This file
// is pure byte<->struct conversion, independent of transport.

#include <cstdint>
#include <cstddef>
#include <optional>

namespace trumastatus {

constexpr uint8_t STATUS_BUFFER_PREAMBLE[10] = {
    0x00, 0x1E, 0x00, 0x00, 0x22, 0xFF, 0xFF, 0xFF, 0x54, 0x01
};

constexpr uint8_t COMMAND_ID_STATUS = 0x33;         // read from CP Plus
constexpr uint8_t COMMAND_ID_STATUS_WRITE = 0x32;   // write to CP Plus (cid - 1)
constexpr uint8_t COMMAND_ID_COMMAND_COUNTER = 0x0D; // counter-only update, no payload

enum class HeatingMode : uint8_t { Off = 0, Eco = 1, Boost = 10 };

struct MainStatus {
    // --- read/write fields (also sent when writing an update) ---
    std::optional<double> target_temp_room_celsius;
    HeatingMode heating_mode = HeatingMode::Off;
    std::optional<double> target_temp_water_celsius; // 0/40/60/200 -> off/eco/high/boost
    uint16_t el_power_level = 0;
    uint8_t energy_mix = 0;

    // --- read-only fields (only valid when parsed from a CP Plus status frame) ---
    std::optional<double> current_temp_water_celsius;
    std::optional<double> current_temp_room_celsius;
    uint8_t operating_status = 0;
    int error_code = 0; // 0 = no error
};

// Parses a 20-byte payload (the bytes AFTER the 10-byte preamble + 4-byte
// header, i.e. exactly what inetbox.py's `command.parse(status_buffer[14:])`
// receives) into a MainStatus. Returns false if `len` < 20.
bool parse_main_status(const uint8_t *payload, size_t len, MainStatus *out);

// Serializes the write-side fields of `status` into a 12-byte buffer
// (target_temp_room, heating_mode, _u3=0, el_power_level x2, target_temp_water,
// energy_mix x2) -- the payload inetbox.py sends for command id 0x32 (write).
// `out` must have capacity >= 12. Returns the number of bytes written (12).
size_t serialize_main_status_write(const MainStatus &status, uint8_t *out, size_t out_capacity);

} // namespace trumastatus
