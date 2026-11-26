#pragma once
// Port of victron-ble (keshavdv/victron-ble)'s AES-128-CTR BLE advertisement
// decryption, using our own aes128.hpp (no external crypto library needed --
// keeps this buildable on nRF52 Arduino the same way as the host tests).
//
// Victron "Instant Readout" BLE manufacturer-data format (verified against
// the Python reference implementation, not guessed):
//   bytes[0:2]  prefix          (uint16 LE, Victron company identifier echo)
//   bytes[2:4]  model_id        (uint16 LE)
//   bytes[4]    readout_type    (uint8)
//   bytes[5:7]  iv              (uint16 LE) -- AES-CTR initial counter value
//   bytes[7:]   encrypted_data  -- byte[0] is an unencrypted "key check" byte
//                                  (must equal advertisement_key[0]); the
//                                  rest is AES-128-CTR ciphertext, counter
//                                  initialised to `iv` as a 128-bit
//                                  little-endian integer (i.e. counter block
//                                  = iv-as-u16-LE followed by 14 zero bytes).
//
// This implementation supports payloads up to 16 bytes after the key-check
// byte (a single AES block) -- sufficient for every known Victron record
// type (BatteryMonitor/SmartShunt and SolarCharger/SmartSolar decrypted
// payloads are both exactly 16 bytes per the reference test vectors).

#include <cstdint>
#include <cstddef>

namespace victronble {

struct Container {
    uint16_t prefix;
    uint16_t model_id;
    uint8_t readout_type;
    uint16_t iv;
    const uint8_t *encrypted_data; // points into the original buffer
    size_t encrypted_data_len;
};

// Parses the 7-byte container header + trailing encrypted_data pointer.
// `data`/`len` is the raw BLE manufacturer-data payload (Victron's part,
// already stripped of any outer BLE AD-structure framing).
Container parse_container(const uint8_t *data, size_t len);

enum class DecryptError { None, KeyMismatch, PayloadTooLong, TooShort };

// Decrypts container.encrypted_data (minus its leading key-check byte) into
// `out` (caller-provided buffer, at least 16 bytes). Returns the number of
// plaintext bytes written (encrypted_data_len - 1), or 0 on error (check
// `*error` for the reason). `key` must be exactly 16 bytes.
size_t decrypt(const Container &container, const uint8_t key[16],
                uint8_t *out, size_t out_capacity, DecryptError *error);

} // namespace victronble
