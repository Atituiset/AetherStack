#ifndef AETHER_COMMON_CRYPTO_H
#define AETHER_COMMON_CRYPTO_H

// M12: self-contained cryptographic primitives (no external dependencies).
// Implementations follow the published specifications and are validated
// against official test vectors in stack/tests/test_crypto.cpp.
//
//   * SHA-256   (FIPS 180-4)
//   * HMAC-SHA256 (RFC 2104 / FIPS 198-1)
//   * ChaCha20  (RFC 8439) — stream cipher for user-plane confidentiality

#include <array>
#include <string>
#include <cstdint>
#include <vector>

namespace crypto {

constexpr size_t kSha256Size = 32;
constexpr size_t kKey256Size = 32;
constexpr size_t kNonce96Size = 12;
using Nonce96 = std::array<uint8_t, kNonce96Size>;

// ---- SHA-256 ---------------------------------------------------------------
std::array<uint8_t, kSha256Size> sha256(const uint8_t* data, size_t len);
std::array<uint8_t, kSha256Size> sha256(const std::vector<uint8_t>& data);

// ---- HMAC-SHA256 -----------------------------------------------------------
std::array<uint8_t, kSha256Size> hmac_sha256(const uint8_t* key, size_t key_len,
                                             const uint8_t* data, size_t len);
std::array<uint8_t, kSha256Size> hmac_sha256(
    const std::array<uint8_t, kKey256Size>& key,
    const std::vector<uint8_t>& data);

// ---- ChaCha20 (RFC 8439) ---------------------------------------------------
// keystream_block(key, nonce, counter) -> 64 raw bytes (test-vector access).
std::array<uint8_t, 64> chacha20_block(const std::array<uint8_t, kKey256Size>& key,
                                       const std::array<uint8_t, kNonce96Size>& nonce,
                                       uint32_t counter);

// Encrypt/decrypt (XOR with keystream). Symmetric; counter starts at `counter`
// and advances every 64 bytes of plaintext.
std::vector<uint8_t> chacha20_xor(const std::array<uint8_t, kKey256Size>& key,
                                  const std::array<uint8_t, kNonce96Size>& nonce,
                                  uint32_t counter,
                                  const std::vector<uint8_t>& data);

// ---- helpers ----------------------------------------------------------------
std::vector<uint8_t> to_bytes(const std::string& s);

}

#endif
