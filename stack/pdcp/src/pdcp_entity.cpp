#include "pdcp/pdcp_entity.h"
#include "common/logger.h"
#include <cstring>
#include <stdexcept>

namespace pdcp {

std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu;
    pdu.reserve(PDCP_HEADER_SIZE + sdu.size());
    pdu.push_back(0x00);
    pdu.push_back(0x00);
    pdu.insert(pdu.end(), sdu.begin(), sdu.end());
    return pdu;
}

std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu) {
    if (pdu.size() < PDCP_HEADER_SIZE) {
        return {};
    }
    return std::vector<uint8_t>(pdu.begin() + PDCP_HEADER_SIZE, pdu.end());
}

}


// ---- M12/M13: confidentiality + integrity -----------------------------------

namespace pdcp {

namespace {
constexpr int kSecHeaderSize = 9;  // flags(1) + seq(8)
constexpr int kMacISize = 4;       // truncated HMAC-SHA256
}

std::vector<uint8_t> protect(
    const std::array<uint8_t, crypto::kKey256Size>& key, uint64_t seq,
    const std::vector<uint8_t>& sdu) {
    crypto::Nonce96 nonce{}; // seq in the first 8 bytes, rest zeroed
    for (int i = 0; i < 8; ++i) {
        nonce[i] = static_cast<uint8_t>(seq >> (8 * i));
    }
    std::vector<uint8_t> out(kSecHeaderSize);
    out[0] = kPdcpFlagEncrypted | kPdcpFlagIntegrity;
    for (int i = 0; i < 8; ++i) {
        out[1 + i] = nonce[i];
    }
    auto ct = crypto::chacha20_xor(key, nonce, 0, sdu);
    out.insert(out.end(), ct.begin(), ct.end());
    // MAC-I covers the sequence number and the ciphertext.
    std::vector<uint8_t> mac_input(out.begin() + 1, out.end());
    auto mac = crypto::hmac_sha256(key, mac_input);
    out.insert(out.end(), mac.begin(), mac.begin() + kMacISize);
    return out;
}

bool unprotect(const std::array<uint8_t, crypto::kKey256Size>& key,
               const std::vector<uint8_t>& pdu, std::vector<uint8_t>& sdu) {
    const bool integrity = (pdu.size() > kSecHeaderSize &&
                            (pdu[0] & kPdcpFlagIntegrity) != 0);
    const size_t min_size =
        static_cast<size_t>(kSecHeaderSize) +
        (integrity ? kMacISize : 0);
    if (pdu.size() < min_size || !(pdu[0] & kPdcpFlagEncrypted)) {
        return false;
    }
    if (integrity) {
        // Verify before decrypting: MAC over seq||ciphertext.
        std::vector<uint8_t> mac_input(pdu.begin() + 1, pdu.end() - kMacISize);
        auto mac = crypto::hmac_sha256(key, mac_input);
        if (std::memcmp(mac.data(), pdu.data() + pdu.size() - kMacISize,
                        kMacISize) != 0) {
            LOG_WARN(ev::PDCP_MAC_FAIL, {});
            return false;
        }
    }
    // Nonce = the 8-byte sequence from the header, zero-padded to 12 bytes
    // (must match protect(); reading further would pull in ciphertext).
    crypto::Nonce96 nonce{};
    for (int i = 0; i < 8; ++i) {
        nonce[i] = pdu[1 + i];
    }
    const size_t ct_end = integrity ? pdu.size() - kMacISize : pdu.size();
    auto ct = std::vector<uint8_t>(pdu.begin() + kSecHeaderSize,
                                   pdu.begin() + static_cast<long>(ct_end));
    sdu = crypto::chacha20_xor(key, nonce, 0, ct);
    return true;
}

}
