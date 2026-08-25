#ifndef AETHER_PDCP_PDCP_ENTITY_H
#define AETHER_PDCP_PDCP_ENTITY_H

#include "common/crypto.h"
#include <cstdint>
#include <vector>

namespace pdcp {

constexpr uint8_t PDCP_HEADER_SIZE = 2;

std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu);
std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu);

// ---- M12: confidentiality -----------------------------------------------
// Wire format: [flags:1][seq:8 LE][payload]
//   flags bit0 = encrypted. payload is ChaCha20(sdu, key, nonce=seq).
// A plaintext legacy frame has flags=0 and no seq field.

constexpr uint8_t kPdcpFlagEncrypted = 0x01;

std::vector<uint8_t> protect(const std::array<uint8_t, crypto::kKey256Size>& key,
                             uint64_t seq,
                             const std::vector<uint8_t>& sdu);

// Returns false when the frame is malformed or the key does not match.
bool unprotect(const std::array<uint8_t, crypto::kKey256Size>& key,
               const std::vector<uint8_t>& pdu, std::vector<uint8_t>& sdu);

}

#endif
