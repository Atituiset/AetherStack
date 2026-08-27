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
//   0 = CCCH (Common Control Channel, used for RACH MSG3 / RRC setup)
//   1 = NAS DCCH (PDCP+RLC wrapped)
//   2 = App DTCH (user plane, PDCP+RLC wrapped)
//   61/62 = SIB1 / MIB system broadcast (BS -> all UEs, rnti 0xFFFF)
//   63 = padding

constexpr uint8_t LCID_CCCH = 0;
constexpr uint8_t LCID_NAS_DCCH = 1;
constexpr uint8_t LCID_APP_DTCH = 2;
// M17: QoS dedicated bearers — one logical channel per service class.
constexpr uint8_t LCID_APP_SIG = 3;   // QCI5  SIP-lite call control (AM)
constexpr uint8_t LCID_APP_VOICE = 4; // QCI1  conversational voice (AM)
constexpr uint8_t LCID_APP_VIDEO = 5; // QCI2  conversational video (AM)
constexpr uint8_t LCID_HARQ_ACK = 60; // 0x3C control: [proc][ack]
constexpr uint8_t LCID_RLC_STATUS = 59; // 0x3B control: RLC AM STATUS PDU (default bearer)
constexpr uint8_t LCID_PAGING = 58; // 0x3A control: paging record (IMSI)
// M17: per-bearer AM STATUS LCIDs (default bearer keeps LCID_RLC_STATUS;
// voice/video run UM and have no STATUS).
constexpr uint8_t LCID_RLC_STATUS_SIG = 57;   // 0x39
// M19: link-adaptation / power-control MAC CEs (single-byte payloads).
constexpr uint8_t LCID_CQI_REPORT = 56;       // 0x38 UL: [cqi:1]
constexpr uint8_t LCID_TPC = 55;              // 0x37 DL: [cmd:1 signed dB]
constexpr uint8_t LCID_SIB1 = 61; // 0x3D
constexpr uint8_t LCID_MIB = 62;  // 0x3E
constexpr uint8_t LCID_PADDING = 63; // 0x3F
constexpr uint16_t RNTI_BROADCAST = 0xFFFF;

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
