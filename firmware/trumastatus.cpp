#include "trumastatus.hpp"
#include "trumaconv.hpp"
#include <cstring>

namespace trumastatus {

static inline uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

bool parse_main_status(const uint8_t *payload, size_t len, MainStatus *out) {
    if (len < 20 || out == nullptr) return false;

    uint16_t target_temp_room_code = read_u16_le(&payload[0]);
    uint8_t heating_mode_raw = payload[2];
    // payload[3] = _recv_status_u3, unused
    uint16_t el_power_level = read_u16_le(&payload[4]);
    uint16_t target_temp_water_code = read_u16_le(&payload[6]);
    // payload[8:10] duplicate el_power_level per the reference's field list
    uint8_t energy_mix = payload[10];
    // payload[11] duplicate energy_mix

    uint16_t current_temp_water_code = read_u16_le(&payload[12]);
    uint16_t current_temp_room_code = read_u16_le(&payload[14]);
    uint8_t operating_status = payload[16];
    // error_code is a raw 2-byte pair, decoded as byte[1]*100 + byte[0]
    int error_code = trumaconv::error_code_from_bytes(payload[17], payload[18]);
    // payload[19] = _recv_status_u10, unused

    out->target_temp_room_celsius = trumaconv::temp_code_to_celsius(target_temp_room_code);
    out->heating_mode = (HeatingMode)heating_mode_raw;
    out->target_temp_water_celsius = trumaconv::temp_code_to_celsius(target_temp_water_code);
    out->el_power_level = el_power_level;
    out->energy_mix = energy_mix;

    out->current_temp_water_celsius = trumaconv::temp_code_to_celsius(current_temp_water_code);
    out->current_temp_room_celsius = trumaconv::temp_code_to_celsius(current_temp_room_code);
    out->operating_status = operating_status;
    out->error_code = error_code;

    return true;
}

size_t serialize_main_status_write(const MainStatus &status, uint8_t *out, size_t out_capacity) {
    if (out_capacity < 12) return 0;
    memset(out, 0, 12);

    uint16_t target_temp_room_code = trumaconv::celsius_to_temp_code(
        status.target_temp_room_celsius.has_value() ? *status.target_temp_room_celsius : 0.0);
    write_u16_le(&out[0], target_temp_room_code);

    out[2] = (uint8_t)status.heating_mode;
    out[3] = 0; // _recv_status_u3 reserved

    write_u16_le(&out[4], status.el_power_level);

    uint16_t target_temp_water_code = trumaconv::celsius_to_temp_code(
        status.target_temp_water_celsius.has_value() ? *status.target_temp_water_celsius : 0.0);
    write_u16_le(&out[6], target_temp_water_code);

    write_u16_le(&out[8], status.el_power_level); // duplicate field, per reference layout

    out[10] = status.energy_mix;
    out[11] = status.energy_mix; // duplicate field, per reference layout

    return 12;
}

} // namespace trumastatus
