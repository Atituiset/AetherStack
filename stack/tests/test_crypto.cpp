// M12a: cryptographic primitives validated against official test vectors.
#include "common/crypto.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>

using namespace crypto;

namespace {
std::string hex(const std::array<uint8_t, kSha256Size>& h) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto b : h) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}
} // namespace

TEST(Sha256, FipsVectors) {
    EXPECT_EQ(hex(sha256(to_bytes(""))),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hex(sha256(to_bytes("abc"))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(hex(sha256(to_bytes("abcdbcdecdefdefgefghfghighijhi"
                                  "jkijkljklmklmnlmnomnopnopq"))),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, LongInputMultiBlock) {
    std::vector<uint8_t> million(1000000, uint8_t('a'));
    EXPECT_EQ(hex(sha256(million)),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(HmacSha256, Rfc4231Vector2) {
    // RFC 4231 test case 2: key "Jefe", data "what do ya want for nothing?"
    std::array<uint8_t, kKey256Size> key{};
    std::memcpy(key.data(), "Jefe", 4);
    auto mac = hmac_sha256(key, to_bytes("what do ya want for nothing?"));
    EXPECT_EQ(hex(mac),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(HmacSha256, KeyLongerThanBlockIsHashed) {
    std::vector<uint8_t> bigkey(131, 0xaa);
    auto data = to_bytes(
        "Test Using Larger Than Block-Size Key - Hash Key First");
    auto mac = hmac_sha256(bigkey.data(), bigkey.size(), data.data(),
                           data.size());
    EXPECT_EQ(hex(mac).substr(0, 16), "60e431591ee0b67f");
}

TEST(ChaCha20, Rfc8439Section21Vector) {
    // RFC 8439 section 2.1.1 (block function) / 2.4.2 encryption vector.
    std::array<uint8_t, kKey256Size> key{};
    const uint8_t k[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
        0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
    std::memcpy(key.data(), k, key.size());
    std::array<uint8_t, kNonce96Size> nonce{0,0,0,0x09,0,0,0,0x4a,0,0,0,0};
    auto ks = chacha20_block(key, nonce, 1);
    // First 16 keystream bytes from RFC 8439 2.1.1 block counter=1.
    const uint8_t expect[16] = {0x10,0xf1,0xe7,0xe4,0xd1,0x3b,0x59,0x15,
                                0x50,0x0f,0xdd,0x1f,0xa3,0x20,0x71,0xc4};
    EXPECT_TRUE(std::memcmp(ks.data(), expect, 16) == 0);
}

TEST(ChaCha20, Rfc8439EncryptionRoundTrip) {
    std::array<uint8_t, kKey256Size> key{};
    const uint8_t k[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
        0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
    std::memcpy(key.data(), k, key.size());
    std::array<uint8_t, kNonce96Size> nonce{0,0,0,0,0,0,0,0x4a,0,0,0,0};
    const char* sunscreen = "Ladies and Gentlemen of the class of '99: If I "
        "could offer you only one tip for the future, sunscreen would be it.";
    auto pt = to_bytes(sunscreen);
    auto ct = chacha20_xor(key, nonce, 1, pt);
    // Known first bytes of ciphertext (RFC 8439 2.4.2).
    EXPECT_EQ(ct[0], 0x6e); EXPECT_EQ(ct[1], 0x2e); EXPECT_EQ(ct[2], 0x35);
    EXPECT_EQ(ct[3], 0x9a);
    auto back = chacha20_xor(key, nonce, 1, ct);
    EXPECT_EQ(back, pt); // symmetric decryption restores plaintext
}
