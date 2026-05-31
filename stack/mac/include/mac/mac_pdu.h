#ifndef AETHER_MAC_MAC_PDU_H
#define AETHER_MAC_MAC_PDU_H

#include <cstdint>
#include <utility>
#include <vector>

namespace mac {

// Simplified MAC subheader:
//   1 byte: [R(1) | F(1) | LCID(6)]
//   If F=0: 1 byte L (payload length, max 255)
//   If F=1: 2 bytes L (payload length, max 65535)
//
// MAC PDU = [subheader1 + payload1] [subheader2 + payload2] ... [optional padding]
//
// Special LCIDs:
//   0 = CCCH (Common Control Channel, used for RACH MSG3)
//   1-32 = logical channels

constexpr uint8_t LCID_CCCH = 0;
constexpr uint8_t LCID_PADDING = 63; // 0x3F

struct MacSubheader {
    uint8_t lcid = 0;
    uint16_t length = 0; // payload length in bytes
};

// Build a MAC PDU from one or more (lcid, sdu) pairs
std::vector<uint8_t> build_pdu(const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& sdus);

// Parse a MAC PDU into (lcid, sdu) pairs
std::vector<std::pair<uint8_t, std::vector<uint8_t>>> parse_pdu(const std::vector<uint8_t>& pdu);

}

#endif
