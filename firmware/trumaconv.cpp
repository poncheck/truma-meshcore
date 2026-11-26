#include "trumaconv.hpp"

namespace trumaconv {

double temp_code_to_celsius(uint16_t code) {
    // Port of inetbox.py temp_code_to_decimal:
    //   if bytestring in (0xAAA, 0xAAAA, 0x0000): return "0"
    //   return (bytestring / 10) - 273
    if (code == 0xAAA || code == 0xAAAA || code == 0x0000) {
        return 0.0;
    }
    return (static_cast<double>(code) / 10.0) - 273.0;
}

uint16_t celsius_to_temp_code(double celsius) {
    // Port of inetbox.py decimal_to_temp_code:
    //   if decimal is None or decimal < 5: return 0x00
    //   return int((decimal + 273) * 10)
    if (celsius < 5.0) {
        return 0x00;
    }
    return static_cast<uint16_t>((celsius + 273.0) * 10.0);
}

WaterTarget water_temp_code_to_target(uint16_t code) {
    double c = temp_code_to_celsius(code);
    // TRANSLATIONS_STATES["none"]["target_temp_water"]: {"0":"0","40":"40","60":"60","200":"200"}
    // i.e. the only valid decimal values are 0, 40, 60, 200 (mapped by the
    // caller's language table to off/eco/high/boost in the "de"/"en" tables).
    if (c == 0.0) return WaterTarget::Off;
    if (c == 40.0) return WaterTarget::Eco;
    if (c == 60.0) return WaterTarget::High;
    if (c == 200.0) return WaterTarget::Boost;
    return WaterTarget::Unknown;
}

uint16_t water_target_to_temp_code(WaterTarget target) {
    switch (target) {
        case WaterTarget::Off:   return celsius_to_temp_code(0.0);
        case WaterTarget::Eco:   return celsius_to_temp_code(40.0);
        case WaterTarget::High:  return celsius_to_temp_code(60.0);
        case WaterTarget::Boost: return celsius_to_temp_code(200.0);
        default: return 0x00;
    }
}

HeatingMode heating_mode_from_code(uint8_t code) {
    // TRANSLATIONS_STATES["none"]["heating_mode"]: {0:"off", 1:"eco", 10:"boost"}
    switch (code) {
        case 0: return HeatingMode::Off;
        case 1: return HeatingMode::Eco;
        case 10: return HeatingMode::Boost;
        default: return HeatingMode::Unknown;
    }
}

uint8_t heating_mode_to_code(HeatingMode mode) {
    switch (mode) {
        case HeatingMode::Off: return 0;
        case HeatingMode::Eco: return 1;
        case HeatingMode::Boost: return 10;
        default: return 0;
    }
}

EnergyMix energy_mix_from_code(uint8_t code) {
    // TRANSLATIONS_STATES["none"]["energy_mix"]: {0b00:"none",0b01:"gas",0b10:"electricity",0b11:"mix"}
    switch (code & 0x03) {
        case 0b00: return EnergyMix::None;
        case 0b01: return EnergyMix::Gas;
        case 0b10: return EnergyMix::Electricity;
        case 0b11: return EnergyMix::Mix;
    }
    return EnergyMix::Unknown;
}

uint8_t energy_mix_to_code(EnergyMix mix) {
    switch (mix) {
        case EnergyMix::None: return 0b00;
        case EnergyMix::Gas: return 0b01;
        case EnergyMix::Electricity: return 0b10;
        case EnergyMix::Mix: return 0b11;
        default: return 0b00;
    }
}

int error_code_from_bytes(uint8_t low, uint8_t high) {
    // Port of inetbox.py error_code_to_string:
    //   error_code = error_code_bytes[1] * 100 + error_code_bytes[0]
    return static_cast<int>(high) * 100 + static_cast<int>(low);
}

} // namespace trumaconv
