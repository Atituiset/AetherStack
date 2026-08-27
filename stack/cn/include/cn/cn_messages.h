#ifndef AETHER_CN_CN_MESSAGES_H
#define AETHER_CN_CN_MESSAGES_H

// M15: core-network interface messages. Wire format mirrors NasMessage /
// RrcMessage: [type:1][len:2 LE][value...]. Two families share one envelope:
//
//   NG-like (gNB <-> AMF, control plane, cf. NGAP):
//     NG_SETUP          gNB announces itself            {cell_id:2}
//     NG_SETUP_OK       AMF accepts                     {}
//     INITIAL_UE_MSG    gNB -> AMF, first NAS uplink    {rnti:2} ++ nas_pdu
//     UPLINK_NAS        gNB -> AMF                      {tmsi:4}{rnti:2} ++ pdu
//     DOWNLINK_NAS      AMF -> gNB                      {tmsi:4}{rnti:2} ++ pdu
//     SESSION_KEY       AMF -> gNB after auth           {tmsi:4}{rnti:2} key(32)
//     PAGING_REQ        AMF -> gNB                      imsi
//     UE_CTX_RELEASE    gNB -> AMF (detach / HO done)   {tmsi:4}
//     HO_REQUIRED       source gNB -> AMF               {tmsi:4}{rnti:2}{tgt:2}
//                       ctx = tmsi(4) rnti(2) sec_on(1) key(32) imsi_len(1) imsi
//     HO_COMMAND        AMF -> target gNB               same ctx payload
//     HO_NOTIFY         target gNB -> AMF (UE landed)   {tmsi:4}{new_rnti:2}
//
//   GTP-U-like (gNB <-> UPF, user plane, cf. GTP-U):
//     UL_DATA           gNB -> UPF                      {tmsi:4}{rnti:2} ++ pdu
//     DL_DATA           UPF -> gNB (routed to serving)  {tmsi:4}{rnti:2} ++ pdu
//     PATH_SWITCH       gNB -> UPF (HO landed here)     {tmsi:4}{rnti:2}

#include <cstdint>
#include <string>
#include <vector>

namespace cn {

enum class MsgType : uint8_t {
    // control plane
    NG_SETUP = 1,
    NG_SETUP_OK = 2,
    INITIAL_UE_MSG = 3,
    UPLINK_NAS = 4,
    DOWNLINK_NAS = 5,
    SESSION_KEY = 6,
    PAGING_REQ = 7,
    UE_CTX_RELEASE = 8,
    HO_REQUIRED = 9,
    HO_COMMAND = 10,
    HO_NOTIFY = 11,
    HO_PREPARED = 12,     // AMF -> all gNBs {tmsi:4}{tgt:2}{new_rnti:2}
    // user plane
    UL_DATA = 32,
    DL_DATA = 33,
    PATH_SWITCH = 34,
    // M22: Xn interface between two gNBs (dual-BS mobility).
    XN_HO_PREPARE = 40,     // source -> target {tmsi:4}{from:2}{to:2}{sec:1}{key:32}{imsi_len:1}{imsi}
    XN_HO_PREPARE_ACK = 41, // target -> source {tmsi:4}{new_rnti:2}
    XN_FWD_DATA = 42,       // either direction: U2U SDU for a UE that
                            // moved to the peer {imsi_len:1}{imsi}{sdu}
    XN_HO_COMPLETE = 43,    // target -> source {tmsi:4}{new_rnti:2}:
                            // the UE confirmed; release the source context
};

struct CnMessage {
    MsgType msg_type = MsgType::NG_SETUP;
    std::vector<uint8_t> value;

    std::vector<uint8_t> encode() const;
    static CnMessage decode(const std::vector<uint8_t>& data);
};

// ---- little-endian field helpers shared by the entity implementations -----
inline void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}
inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
}
inline uint16_t get16(const std::vector<uint8_t>& v, size_t off) {
    return static_cast<uint16_t>(v[off] | (v[off + 1] << 8));
}
inline uint32_t get32(const std::vector<uint8_t>& v, size_t off) {
    return static_cast<uint32_t>(v[off]) | (static_cast<uint32_t>(v[off + 1]) << 8) |
           (static_cast<uint32_t>(v[off + 2]) << 16) |
           (static_cast<uint32_t>(v[off + 3]) << 24);
}

} // namespace cn

#endif
