#include "victronble.hpp"
#include "aes128.hpp"
#include <cstring>

namespace victronble {

static inline uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

Container parse_container(const uint8_t *data, size_t len) {
    Container c{};
    if (len < 7) {
        return c; // caller must separately validate len >= 7 before use
    }
    c.prefix = read_u16_le(&data[0]);
    c.model_id = read_u16_le(&data[2]);
    c.readout_type = data[4];
    c.iv = read_u16_le(&data[5]);
    c.encrypted_data = &data[7];
    c.encrypted_data_len = len - 7;
    return c;
}

size_t decrypt(const Container &container, const uint8_t key[16],
                uint8_t *out, size_t out_capacity, DecryptError *error) {
    if (error) *error = DecryptError::None;

    if (container.encrypted_data_len < 1) {
        if (error) *error = DecryptError::TooShort;
        return 0;
    }

    // First byte is an unencrypted key-check byte -- must match key[0].
    if (container.encrypted_data[0] != key[0]) {
        if (error) *error = DecryptError::KeyMismatch;
        return 0;
    }

    size_t payload_len = container.encrypted_data_len - 1; // bytes after key-check
    if (payload_len > 16) {
        // Our single-block implementation only supports up to one AES
        // block of ciphertext (16 bytes) -- true for every known Victron
        // record type, but guard explicitly rather than silently truncate.
        if (error) *error = DecryptError::PayloadTooLong;
        return 0;
    }
    if (payload_len > out_capacity) {
        if (error) *error = DecryptError::PayloadTooLong;
        return 0;
    }

    // Counter.new(128, initial_value=iv, little_endian=True) in pycryptodome:
    // a 128-bit counter whose integer value is `iv`, serialized little-endian
    // into the counter block fed to AES. Since iv fits in 16 bits, the block
    // is simply [iv_lo, iv_hi, 0, 0, ..., 0] (14 trailing zero bytes).
    uint8_t counter_block[16] = {0};
    counter_block[0] = (uint8_t)(container.iv & 0xFF);
    counter_block[1] = (uint8_t)((container.iv >> 8) & 0xFF);

    uint8_t keystream[16];
    memcpy(keystream, counter_block, 16);
    aes128::encrypt_block(key, keystream);

    for (size_t i = 0; i < payload_len; i++) {
        out[i] = container.encrypted_data[1 + i] ^ keystream[i];
    }
    return payload_len;
}

} // namespace victronble
