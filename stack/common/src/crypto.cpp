#include "common/crypto.h"
#include <cstring>
#include <stdexcept>

namespace crypto {

namespace {

inline uint32_t rotl(uint32_t v, int c) {
    return (v << c) | (v >> (32 - c));
}
inline uint32_t rotr(uint32_t v, int c) {
    return (v >> c) | (v << (32 - c));
}

// ---- SHA-256 internals -----------------------------------------------------
struct Sha256Ctx {
    uint32_t h[8];
    uint64_t total_bits = 0;
    uint8_t buf[64];
    size_t buf_len = 0;

    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
        0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
        0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
        0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    void init() {
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372;
        h[3] = 0xa54ff53a; h[4] = 0x510e527f; h[5] = 0x9b05688c;
        h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
        total_bits = 0; buf_len = 0;
    }

    static uint32_t load_be(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }
    static void store_be(uint8_t* p, uint32_t v) {
        p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
    }

    void compress(const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = load_be(block + 4 * i);
        for (int i = 16; i < 64; ++i) {
            // SHA-256 sigma functions use ROTATION (not left-rotation).
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                          (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                          (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* data, size_t len) {
        total_bits += len * 8;
        while (len > 0) {
            size_t take = std::min(len, size_t(64) - buf_len);
            std::memcpy(buf + buf_len, data, take);
            buf_len += take; data += take; len -= take;
            if (buf_len == 64) { compress(buf); buf_len = 0; }
        }
    }

    std::array<uint8_t, kSha256Size> finalise() {
        uint64_t bits = total_bits;
        // Build the padding in a side buffer and let update() handle block
        // boundaries (a naive in-buffer write loses the marker when the
        // pending length exceeds 55 bytes).
        uint8_t tail[72];
        size_t t = 0;
        tail[t++] = 0x80;
        while ((buf_len + t) % 64 != 56) tail[t++] = 0x00;
        for (int i = 0; i < 8; ++i) {
            tail[t++] = uint8_t(bits >> (56 - 8 * i));
        }
        update(tail, t);
        std::array<uint8_t, kSha256Size> out{};
        for (int i = 0; i < 8; ++i) store_be(out.data() + 4 * i, h[i]);
        return out;
    }
};

constexpr uint32_t Sha256Ctx::K[64];

// ---- ChaCha20 internals ----------------------------------------------------
void quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotl(d, 16);
    c += d; b ^= c; b = rotl(b, 12);
    a += b; d ^= a; d = rotl(d, 8);
    c += d; b ^= c; b = rotl(b, 7);
}

uint32_t load_le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

constexpr char kSigma[] = "expand 32-byte k";

} // namespace

std::array<uint8_t, kSha256Size> sha256(const uint8_t* data, size_t len) {
    Sha256Ctx ctx;
    ctx.init();
    ctx.update(data, len);
    return ctx.finalise();
}

std::array<uint8_t, kSha256Size> sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

std::array<uint8_t, kSha256Size> hmac_sha256(const uint8_t* key, size_t key_len,
                                             const uint8_t* data, size_t len) {
    uint8_t kblock[64] = {0};
    if (key_len > 64) {
        auto k = sha256(key, key_len);
        std::memcpy(kblock, k.data(), k.size());
    } else {
        std::memcpy(kblock, key, key_len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = kblock[i] ^ 0x36;
        opad[i] = kblock[i] ^ 0x5c;
    }
    Sha256Ctx inner;
    inner.init(); inner.update(ipad, 64); inner.update(data, len);
    auto ih = inner.finalise();
    Sha256Ctx outer;
    outer.init(); outer.update(opad, 64); outer.update(ih.data(), ih.size());
    return outer.finalise();
}

std::array<uint8_t, kSha256Size> hmac_sha256(
    const std::array<uint8_t, kKey256Size>& key,
    const std::vector<uint8_t>& data) {
    return hmac_sha256(key.data(), key.size(), data.data(), data.size());
}

std::array<uint8_t, 64> chacha20_block(
    const std::array<uint8_t, kKey256Size>& key,
    const std::array<uint8_t, kNonce96Size>& nonce, uint32_t counter) {
    uint32_t st[16] = {
        load_le32(reinterpret_cast<const uint8_t*>(kSigma)),      // "expa"
        load_le32(reinterpret_cast<const uint8_t*>(kSigma + 4)),  // "nd 3"
        load_le32(reinterpret_cast<const uint8_t*>(kSigma + 8)),  // "2-by"
        load_le32(reinterpret_cast<const uint8_t*>(kSigma + 12)), // "te k"
    };
    for (int i = 0; i < 8; ++i) {
        st[4 + i] = load_le32(key.data() + 4 * i);
    }
    st[12] = counter;
    st[13] = load_le32(nonce.data());
    st[14] = load_le32(nonce.data() + 4);
    st[15] = load_le32(nonce.data() + 8);

    uint32_t x[16];
    std::memcpy(x, st, sizeof(x));
    for (int i = 0; i < 10; ++i) { // 20 rounds = 10 double rounds
        quarter_round(x[0], x[4], x[8], x[12]);
        quarter_round(x[1], x[5], x[9], x[13]);
        quarter_round(x[2], x[6], x[10], x[14]);
        quarter_round(x[3], x[7], x[11], x[15]);
        quarter_round(x[0], x[5], x[10], x[15]);
        quarter_round(x[1], x[6], x[11], x[12]);
        quarter_round(x[2], x[7], x[8], x[13]);
        quarter_round(x[3], x[4], x[9], x[14]);
    }
    std::array<uint8_t, 64> out{};
    for (int i = 0; i < 16; ++i) {
        uint32_t v = x[i] + st[i];
        out[4 * i] = v; out[4 * i + 1] = v >> 8;
        out[4 * i + 2] = v >> 16; out[4 * i + 3] = v >> 24;
    }
    return out;
}

std::vector<uint8_t> chacha20_xor(
    const std::array<uint8_t, kKey256Size>& key,
    const std::array<uint8_t, kNonce96Size>& nonce, uint32_t counter,
    const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(data.size());
    size_t off = 0;
    uint32_t ctr = counter;
    while (off < data.size()) {
        auto ks = chacha20_block(key, nonce, ctr++);
        size_t n = std::min(size_t(64), data.size() - off);
        for (size_t i = 0; i < n; ++i) {
            out[off + i] = data[off + i] ^ ks[i];
        }
        off += n;
    }
    return out;
}

std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

}
