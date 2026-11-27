#pragma once
// Port of inetbox.py conversions.py — pure value <-> code conversions for the
// Truma TIN-bus protocol. No Arduino/ESP32 dependency so it is host-testable.

#include <cstdint>
#include <string>

namespace trumaconv {

// Truma encodes temperatures as (value_in_celsius + 273) * 10, i.e. tenths of
// a Kelvin. 0xAAA / 0xAAAA / 0x0000 all mean "no reading" -> 0.0.
// Port of inetbox.py temp_code_to_decimal / decimal_to_temp_code.
double temp_code_to_celsius(uint16_t code);
uint16_t celsius_to_temp_code(double celsius); // celsius < 5.0 => 0x00 ("off")

// Water target temperature only has 4 valid settings on Combi: off/eco/high/boost.
// Port of inetbox.py water_temp_code_to_string / string_to_water_temp_code.
enum class WaterTarget { Off, Eco, High, Boost, Unknown };
WaterTarget water_temp_code_to_target(uint16_t code);
uint16_t water_target_to_temp_code(WaterTarget target);

enum class HeatingMode { Off, Eco, Boost, Unknown };
HeatingMode heating_mode_from_code(uint8_t code);
uint8_t heating_mode_to_code(HeatingMode mode);

enum class EnergyMix { None, Gas, Electricity, Mix, Unknown };
EnergyMix energy_mix_from_code(uint8_t code);
uint8_t energy_mix_to_code(EnergyMix mix);

// error_code_bytes[1] * 100 + error_code_bytes[0], per inetbox.py error_code_to_string.
int error_code_from_bytes(uint8_t low, uint8_t high);

} // namespace trumaconv
