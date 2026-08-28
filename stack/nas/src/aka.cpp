#include "nas/aka.h"
#include <algorithm>

namespace nas::aka {

namespace {

// HMAC-SHA256(K, label || fields...) with a one-byte domain label standing
// in for the MILENAGE operator-variant separation.
template <typename... Parts>
std::array<uint8_t, crypto::kSha256Size> f(const Key& k, uint8_t label,
                                           const Parts&... parts) {
    std::vector<uint8_t> data{label};
    (data.insert(data.end(), parts.begin(), parts.end()), ...);
    return crypto::hmac_sha256(k, data);
}

template <size_t N>
std::array<uint8_t, N> take(const std::array<uint8_t, crypto::kSha256Size>& h) {
    std::array<uint8_t, N> out{};
    std::copy_n(h.begin(), N, out.begin());
    return out;
}

std::array<uint8_t, kAkLen> sqn_bytes(uint64_t sqn) {
    std::array<uint8_t, kAkLen> b{};
    for (int i = 0; i < 6; ++i) {
        b[5 - i] = static_cast<uint8_t>((sqn >> (8 * i)) & 0xFF); // big-endian
    }
    return b;
}

} // namespace

std::array<uint8_t, kAkLen> f5(const Key& k,
                               const std::array<uint8_t, kRandLen>& rand) {
    return take<kAkLen>(f(k, 0x05, rand));
}

std::array<uint8_t, kMacLen> f1(const Key& k, uint64_t sqn,
                                const std::array<uint8_t, kRandLen>& rand,
                                const std::array<uint8_t, kAmfLen>& amf) {
    const auto sb = sqn_bytes(sqn);
    return take<kMacLen>(f(k, 0x01, sb, rand, amf));
}

std::array<uint8_t, kResLen> f2(const Key& k,
                                const std::array<uint8_t, kRandLen>& rand) {
    return take<kResLen>(f(k, 0x02, rand));
}

Key f3(const Key& k, const std::array<uint8_t, kRandLen>& rand) {
    return f(k, 0x03, rand);
}

Key f4(const Key& k, const std::array<uint8_t, kRandLen>& rand) {
    return f(k, 0x04, rand);
}

Key kasme(const Key& ck, const Key& ik,
          const std::array<uint8_t, kAkLen>& sqn_xor_ak) {
    // KDF(CK||IK, "kasme" || SQN^AK): key is the 64-byte CK||IK pair.
    std::vector<uint8_t> key64;
    key64.insert(key64.end(), ck.begin(), ck.end());
    key64.insert(key64.end(), ik.begin(), ik.end());
    std::vector<uint8_t> data{'k', 'a', 's', 'm', 'e'};
    data.insert(data.end(), sqn_xor_ak.begin(), sqn_xor_ak.end());
    return crypto::hmac_sha256(key64.data(), key64.size(), data.data(),
                               data.size());
}

std::array<uint8_t, kAkLen> f5s(const Key& k,
                                const std::array<uint8_t, kRandLen>& rand) {
    return take<kAkLen>(f(k, 0x55, rand));
}

std::array<uint8_t, kMacLen> f1s(const Key& k, uint64_t sqn_ms,
                                 const std::array<uint8_t, kRandLen>& rand,
                                 const std::array<uint8_t, kAmfLen>& amf) {
    const auto sb = sqn_bytes(sqn_ms);
    return take<kMacLen>(f(k, 0x51, sb, rand, amf));
}

Vector generate(const Key& k, uint64_t sqn,
                const std::array<uint8_t, kRandLen>& rand) {
    Vector v;
    v.rand = rand;
    v.sqn = sqn & kSqnMask;
    const auto ak = f5(k, rand);
    const auto sb = sqn_bytes(v.sqn);
    for (size_t i = 0; i < kAkLen; ++i) v.sqn_xor_ak[i] = sb[i] ^ ak[i];
    v.amf = kAmf;
    v.mac = f1(k, v.sqn, rand, v.amf);
    v.xres = f2(k, rand);
    v.ck = f3(k, rand);
    v.ik = f4(k, rand);
    v.kasme = kasme(v.ck, v.ik, v.sqn_xor_ak);
    return v;
}

std::array<uint8_t, kAutnLen> autn(const Vector& v) {
    std::array<uint8_t, kAutnLen> out{};
    std::copy(v.sqn_xor_ak.begin(), v.sqn_xor_ak.end(), out.begin());
    std::copy(v.amf.begin(), v.amf.end(), out.begin() + kAkLen);
    std::copy(v.mac.begin(), v.mac.end(), out.begin() + kAkLen + kAmfLen);
    return out;
}

std::optional<uint64_t> verify_autn(
    const Key& k, const std::array<uint8_t, kRandLen>& rand,
    const std::array<uint8_t, kAutnLen>& autn) {
    std::array<uint8_t, kAkLen> sqn_xor_ak;
    std::array<uint8_t, kAmfLen> amf;
    std::array<uint8_t, kMacLen> mac;
    std::copy_n(autn.begin(), kAkLen, sqn_xor_ak.begin());
    std::copy_n(autn.begin() + kAkLen, kAmfLen, amf.begin());
    std::copy_n(autn.begin() + kAkLen + kAmfLen, kMacLen, mac.begin());
    const auto ak = f5(k, rand);
    uint64_t sqn = 0;
    for (size_t i = 0; i < kAkLen; ++i) {
        sqn = (sqn << 8) | static_cast<uint8_t>(sqn_xor_ak[i] ^ ak[i]);
    }
    const auto xmac = f1(k, sqn, rand, amf);
    if (xmac != mac) return std::nullopt;
    return sqn;
}

std::array<uint8_t, kAutsLen> build_auts(
    const Key& k, uint64_t sqn_ms,
    const std::array<uint8_t, kRandLen>& rand) {
    std::array<uint8_t, kAutsLen> out{};
    const auto ak = f5s(k, rand);
    const auto sb = sqn_bytes(sqn_ms & kSqnMask);
    for (size_t i = 0; i < kAkLen; ++i) out[i] = sb[i] ^ ak[i];
    const auto mac_s = f1s(k, sqn_ms & kSqnMask, rand, kAmf);
    std::copy(mac_s.begin(), mac_s.end(), out.begin() + kAkLen);
    return out;
}

std::optional<uint64_t> verify_auts(
    const Key& k, const std::array<uint8_t, kAutsLen>& auts,
    const std::array<uint8_t, kRandLen>& rand) {
    const auto ak = f5s(k, rand);
    uint64_t sqn_ms = 0;
    for (size_t i = 0; i < kAkLen; ++i) {
        sqn_ms = (sqn_ms << 8) | static_cast<uint8_t>(auts[i] ^ ak[i]);
    }
    std::array<uint8_t, kMacLen> mac_s;
    std::copy_n(auts.begin() + kAkLen, kMacLen, mac_s.begin());
    if (f1s(k, sqn_ms, rand, kAmf) != mac_s) return std::nullopt;
    return sqn_ms;
}

std::string hex_prefix(const uint8_t* data, size_t len, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < len && i < n; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xF]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

} // namespace nas::aka

// ---- TRIAL SEED: 跨函数判空契约试验件（三模式对比），勿合并 ----
// 契约：分配处兜底（abort），下游函数不重复判空。
// 模式 1（无 codegraph）预期：trial_fill 解引用被报疑似 cwe-476（FP）
// 模式 2/3（codegraph 链回溯）预期：回溯到 alloc_or_die 的 abort 守卫 → 证伪不报
#include <cstdlib>
#include <cstring>

namespace nas::trial {

static char *alloc_or_die(size_t n) {
    char *p = static_cast<char *>(std::malloc(n));
    if (!p) {
        std::abort(); // 契约锚点：此函数永不返回空指针
    }
    return p;
}

static void fill(char *p, const char *tag) {
    std::strcpy(p, tag); // 依赖调用方契约：p 必非空——本函数不重复判空
}

char *build_banner(const char *tag) {
    char *p = alloc_or_die(64);
    fill(p, tag); // 跨 2 层调用的解引用，上游已兜底
    return p;
}

} // namespace nas::trial
