#ifndef AETHER_NAS_AKA_H
#define AETHER_NAS_AKA_H

// M21: 5G-AKA-style authentication and key agreement (3GPP TS 33.501
// structure, algorithm-pragmatic: HMAC-SHA256 with domain-separation
// labels stands in for the MILENAGE/TUAK f1-f5 — see docs/m21_plan.md).
//
// Sizes follow real AKA: RAND 16 B, AUTN 16 B (SQN^AK 6 || AMF 2 || MAC 8),
// RES/XRES 16 B, AUTS 14 B (SQNms^AK* 6 || MAC-S 8), SQN 48 bit,
// AMF 0x8000. CK/IK/KASME are 32 B (HMAC output length).
//
// Wire payloads (nas_messages.h):
//   AUTH_REQUEST  value = [RAND:16][AUTN:16]
//   AUTH_RESPONSE value = [RES:16]
//   AUTH_FAILURE  value = [cause:1][imsi_len:1][imsi][AUTS:14 when synch]
// (the IMSI tag on AUTH_FAILURE is a simulator shortcut so the network can
// attribute the failure without a session lookup — documented deviation).

#include "common/crypto.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nas::aka {

constexpr size_t kRandLen = 16;
constexpr size_t kAutnLen = 16;
constexpr size_t kAkLen = 6;
constexpr size_t kAmfLen = 2;
constexpr size_t kMacLen = 8;
constexpr size_t kResLen = 16;
constexpr size_t kAutsLen = 14;
constexpr uint64_t kSqnMask = (1ULL << 48) - 1;

// NAS causes (real 5GMM values).
constexpr uint8_t kCauseMacFailure = 0x14;
constexpr uint8_t kCauseSynchFailure = 0x15;

using Key = std::array<uint8_t, crypto::kKey256Size>;

inline const std::array<uint8_t, kAmfLen> kAmf = {0x80, 0x00};

// One authentication vector (UDM/AUSF output).
struct Vector {
    std::array<uint8_t, kRandLen> rand{};
    std::array<uint8_t, kAkLen> sqn_xor_ak{};
    std::array<uint8_t, kAmfLen> amf = kAmf;
    std::array<uint8_t, kMacLen> mac{};
    std::array<uint8_t, kResLen> xres{};
    Key ck{}, ik{}, kasme{};
    uint64_t sqn = 0;
};

// ---- f1..f5 (and f1*/f5*) analogs --------------------------------------------
std::array<uint8_t, kAkLen> f5(const Key& k,
                               const std::array<uint8_t, kRandLen>& rand);
std::array<uint8_t, kMacLen> f1(const Key& k, uint64_t sqn,
                                const std::array<uint8_t, kRandLen>& rand,
                                const std::array<uint8_t, kAmfLen>& amf);
std::array<uint8_t, kResLen> f2(const Key& k,
                                const std::array<uint8_t, kRandLen>& rand);
Key f3(const Key& k, const std::array<uint8_t, kRandLen>& rand); // CK
Key f4(const Key& k, const std::array<uint8_t, kRandLen>& rand); // IK
// KASME-like session key from CK||IK bound to the served SQN^AK.
Key kasme(const Key& ck, const Key& ik,
          const std::array<uint8_t, kAkLen>& sqn_xor_ak);
std::array<uint8_t, kAkLen> f5s(const Key& k,
                                const std::array<uint8_t, kRandLen>& rand);
std::array<uint8_t, kMacLen> f1s(const Key& k, uint64_t sqn_ms,
                                 const std::array<uint8_t, kRandLen>& rand,
                                 const std::array<uint8_t, kAmfLen>& amf);

// UDM side: build a fresh vector for `sqn` with a caller-provided RAND.
Vector generate(const Key& k, uint64_t sqn,
                const std::array<uint8_t, kRandLen>& rand);

// AUTN = SQN^AK || AMF || MAC (16 B).
std::array<uint8_t, kAutnLen> autn(const Vector& v);

// UE side: split an AUTN and recompute the expected MAC for it.
// Returns the concealed SQN when the MAC verifies, nullopt otherwise
// (MAC failure — the network could not prove knowledge of K).
std::optional<uint64_t> verify_autn(
    const Key& k, const std::array<uint8_t, kRandLen>& rand,
    const std::array<uint8_t, kAutnLen>& autn);

// UE side: AUTS = SQNms^AK* || MAC-S for the synchronisation failure.
std::array<uint8_t, kAutsLen> build_auts(
    const Key& k, uint64_t sqn_ms,
    const std::array<uint8_t, kRandLen>& rand);

// UDM side: validate an AUTS against (K, RAND of the failed vector) and
// recover the UE's SQNms for resynchronisation.
std::optional<uint64_t> verify_auts(
    const Key& k, const std::array<uint8_t, kAutsLen>& auts,
    const std::array<uint8_t, kRandLen>& rand);

// First `n` bytes as lowercase hex (log masking helper).
std::string hex_prefix(const uint8_t* data, size_t len, size_t n = 8);
template <size_t N>
std::string hex_prefix(const std::array<uint8_t, N>& a, size_t n = 8) {
    return hex_prefix(a.data(), N, n);
}

} // namespace nas::aka

#endif
