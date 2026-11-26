#pragma once
// Minimal, self-contained AES-128 block cipher (encrypt only -- CTR mode
// needs only the forward cipher, never decrypt). No external dependency so
// this compiles identically on the host (g++, for tests) and on the nRF52
// Arduino toolchain. Implements FIPS-197 AES-128: 10 rounds, 16-byte key.
//
// This is deliberately a small, textbook implementation (not
// hardware-accelerated) -- Victron advertisements are tiny (<=16 bytes
// plaintext per packet, received every ~1s at most), so raw throughput is
// a non-issue on an nRF52840 at 64MHz.

#include <cstdint>
#include <cstddef>

namespace aes128 {

// Encrypts exactly one 16-byte block in place: `block` is both input and
// output. `key` must be exactly 16 bytes (AES-128).
void encrypt_block(const uint8_t key[16], uint8_t block[16]);

} // namespace aes128
