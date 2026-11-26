#include "victrondevices.hpp"

namespace victrondevices {

BatteryMonitorReading parse_battery_monitor(const uint8_t *decrypted, size_t len) {
    BitReader reader(decrypted, len);
    BatteryMonitorReading r;

    uint32_t remaining_mins = reader.read_unsigned_int(16);
    int32_t voltage = reader.read_signed_int(16);
    uint32_t alarm = reader.read_unsigned_int(16);
    uint32_t aux = reader.read_unsigned_int(16);
    uint32_t aux_mode = reader.read_unsigned_int(2);
    int32_t current = reader.read_signed_int(22);
    uint32_t consumed_ah = reader.read_unsigned_int(20);
    uint32_t soc = reader.read_unsigned_int(10);

    if (remaining_mins != 0xFFFF) r.remaining_mins = (float)remaining_mins;
    if (voltage != 0x7FFF) r.voltage = voltage / 100.0f;
    r.alarm = (uint16_t)alarm;
    r.aux_mode = (AuxMode)aux_mode;
    if (current != 0x3FFFFF) r.current = current / 1000.0f;
    if (consumed_ah != 0xFFFFF) r.consumed_ah = -(float)consumed_ah / 10.0f;
    if (soc != 0x3FF) r.soc = soc / 10.0f;

    if (aux_mode == (uint32_t)AuxMode::STARTER_VOLTAGE) {
        r.starter_voltage = BitReader::to_signed_int(aux, 16) / 100.0f;
    } else if (aux_mode == (uint32_t)AuxMode::MIDPOINT_VOLTAGE) {
        r.midpoint_voltage = aux / 100.0f;
    } else if (aux_mode == (uint32_t)AuxMode::TEMPERATURE) {
        // aux holds temperature in 0.01 Kelvin increments; convert to Celsius
        // (kelvin_to_celsius: celsius = kelvin - 273.15), matching the
        // reference's two-step get_temperature() which stores Kelvin first.
        float kelvin = aux / 100.0f;
        r.temperature_celsius = kelvin - 273.15f;
    }

    return r;
}

SolarChargerReading parse_solar_charger(const uint8_t *decrypted, size_t len) {
    BitReader reader(decrypted, len);
    SolarChargerReading r;

    uint32_t charge_state = reader.read_unsigned_int(8);
    uint32_t charger_error = reader.read_unsigned_int(8);
    int32_t battery_voltage = reader.read_signed_int(16);
    int32_t battery_charging_current = reader.read_signed_int(16);
    uint32_t yield_today = reader.read_unsigned_int(16);
    uint32_t solar_power = reader.read_unsigned_int(16);
    uint32_t external_device_load = reader.read_unsigned_int(9);

    if (charge_state != 0xFF) r.charge_state = (uint8_t)charge_state;
    if (charger_error != 0xFF) r.charger_error = (uint8_t)charger_error;
    if (battery_voltage != 0x7FFF) r.battery_voltage = battery_voltage / 100.0f;
    if (battery_charging_current != 0x7FFF) r.battery_charging_current = battery_charging_current / 10.0f;
    if (yield_today != 0xFFFF) r.yield_today_wh = yield_today * 10.0f;
    if (solar_power != 0xFFFF) r.solar_power_w = (uint16_t)solar_power;
    if (external_device_load != 0x1FF) r.external_device_load = external_device_load / 10.0f;

    return r;
}

} // namespace victrondevices
