#pragma once
// Port of victron-ble's BitReader + BatteryMonitor (SmartShunt) and
// SolarCharger (SmartSolar) decrypted-payload parsers.
//
// BitReader semantics (verified against victron_ble/devices/base.py):
// reads bits LSB-first within each byte, advancing across byte boundaries,
// exactly mirroring Python's `(self._data[index >> 3] >> (index & 7)) & 1`.

#include <cstdint>
#include <cstddef>
#include <optional>

namespace victrondevices {

class BitReader {
public:
    BitReader(const uint8_t *data, size_t len) : data_(data), len_(len), index_(0) {}

    int read_bit() {
        size_t byte_idx = index_ >> 3;
        int bit_idx = index_ & 7;
        int bit = (byte_idx < len_) ? ((data_[byte_idx] >> bit_idx) & 1) : 0;
        index_++;
        return bit;
    }

    uint32_t read_unsigned_int(int num_bits) {
        uint32_t value = 0;
        for (int position = 0; position < num_bits; position++) {
            value |= (uint32_t)read_bit() << position;
        }
        return value;
    }

    int32_t read_signed_int(int num_bits) {
        return to_signed_int(read_unsigned_int(num_bits), num_bits);
    }

    static int32_t to_signed_int(uint32_t value, int num_bits) {
        uint32_t sign_bit = 1u << (num_bits - 1);
        if (value & sign_bit) {
            return (int32_t)value - (int32_t)(1u << num_bits);
        }
        return (int32_t)value;
    }

private:
    const uint8_t *data_;
    size_t len_;
    size_t index_;
};

enum class AuxMode { STARTER_VOLTAGE = 0, MIDPOINT_VOLTAGE = 1, TEMPERATURE = 2, DISABLED = 3 };

struct BatteryMonitorReading {
    std::optional<float> remaining_mins; // minutes
    std::optional<float> voltage;        // volts
    uint16_t alarm = 0;                  // raw AlarmReason bitmask
    AuxMode aux_mode = AuxMode::DISABLED;
    std::optional<float> current;        // amps
    std::optional<float> consumed_ah;    // amp-hours (negative = discharged)
    std::optional<float> soc;            // state of charge, percent
    std::optional<float> starter_voltage;
    std::optional<float> midpoint_voltage;
    std::optional<float> temperature_celsius;
};

// `decrypted` must be the plaintext payload from victronble::decrypt()
// (16 bytes for every known SmartShunt/BMV record).
BatteryMonitorReading parse_battery_monitor(const uint8_t *decrypted, size_t len);

struct SolarChargerReading {
    std::optional<uint8_t> charge_state;        // raw OperationMode value
    std::optional<uint8_t> charger_error;       // raw ChargerError value
    std::optional<float> battery_voltage;       // volts
    std::optional<float> battery_charging_current; // amps
    std::optional<float> yield_today_wh;        // watt-hours
    std::optional<uint16_t> solar_power_w;      // watts
    std::optional<float> external_device_load;  // amps
};

SolarChargerReading parse_solar_charger(const uint8_t *decrypted, size_t len);

} // namespace victrondevices
