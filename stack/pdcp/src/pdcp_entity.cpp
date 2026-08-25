#include "pdcp/pdcp_entity.h"
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


// ---- M12: confidentiality ---------------------------------------------------

namespace pdcp {

namespace {
constexpr int kSecHeaderSize = 9; // flags(1) + seq(8)
}

std::vector<uint8_t> protect(
    const std::array<uint8_t, crypto::kKey256Size>& key, uint64_t seq,
    const std::vector<uint8_t>& sdu) {
    crypto::Nonce96 nonce{}; // seq in the first 8 bytes, rest zeroed
    for (int i = 0; i < 8; ++i) {
        nonce[i] = static_cast<uint8_t>(seq >> (8 * i));
    }
    std::vector<uint8_t> out(kSecHeaderSize);
    out[0] = kPdcpFlagEncrypted;
    for (int i = 0; i < 8; ++i) {
        out[1 + i] = nonce[i];
    }
    auto ct = crypto::chacha20_xor(key, nonce, 0, sdu);
    out.insert(out.end(), ct.begin(), ct.end());
    return out;
}

bool unprotect(const std::array<uint8_t, crypto::kKey256Size>& key,
               const std::vector<uint8_t>& pdu, std::vector<uint8_t>& sdu) {
    if (pdu.size() < kSecHeaderSize || !(pdu[0] & kPdcpFlagEncrypted)) {
        return false;
    }
    // Nonce = the 8-byte sequence from the header, zero-padded to 12 bytes
    // (must match protect(); reading further would pull in ciphertext).
    crypto::Nonce96 nonce{};
    for (int i = 0; i < 8; ++i) {
        nonce[i] = pdu[1 + i];
    }
    auto ct = std::vector<uint8_t>(pdu.begin() + kSecHeaderSize, pdu.end());
    sdu = crypto::chacha20_xor(key, nonce, 0, ct);
    return true;
}

}
