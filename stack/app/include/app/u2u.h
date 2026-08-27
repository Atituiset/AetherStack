#ifndef AETHER_APP_U2U_H
#define AETHER_APP_U2U_H

// M16: UE-to-UE user plane (voice call / video stream / text message).
//
// A small app-level header rides on top of the legacy user-plane SDU so the
// BS app layer can terminate uplink data and forward it into the peer UE's
// downlink flow. PDCP ciphering still terminates at the BS per hop, exactly
// like a real network; only the payload here is synthetic.
//
// Wire format:
//   [magic:1 = 0xA5][kind:1][seq:4 LE][timestamp_ms:4 LE]
//   [src_imsi_len:1][src_imsi][dst_imsi_len:1][dst_imsi] ++ payload
//
// kind byte: low nibble = MediaKind, bit5 = CONF (M18: a conf_id field
// follows dst_imsi), bit6 = END (stream teardown),
// bit7 = ACK (RTCP-like receiver feedback echoing the media seq).

#include <cstdint>
#include <string>
#include <vector>

namespace app {

enum class MediaKind : uint8_t { MSG = 0, VOICE = 1, VIDEO = 2, SIG = 3 };

constexpr uint8_t kU2uMagic = 0xA5;
constexpr uint8_t kU2uFlagConf = 0x20;
constexpr uint8_t kU2uFlagEnd = 0x40;
constexpr uint8_t kU2uFlagAck = 0x80;
constexpr size_t kU2uMaxImsiLen = 32;

// "msg" | "voice" | "video" | "sig" (the contract strings used in log
// fields; "sig" marks SIP-lite call-control packets, M17).
const char* media_kind_name(MediaKind kind);

// ---- M17: SIP-lite call signaling -------------------------------------------
// Signaling rides the same U2U header with kind=SIG; the payload is:
//   [method:1][call_id:4 LE][media_kind:1]  (6 bytes)
// M18: conference dialogs append [conf_id:4 LE] (10 bytes). Receivers MUST
// ignore payloads that are shorter, longer (forward-compat trailing
// fields) or carry an unknown method, keeping the format version-tolerant.
// RLC AM below makes delivery reliable + in-order, so no retransmission
// timers exist at this layer.
enum class SigMethod : uint8_t {
    INVITE = 1,
    RINGING_180 = 2,
    OK_200 = 3,
    ACK = 4,
    BYE = 5,
    OK_BYE = 6,      // 200 OK confirming a BYE
    BUSY_486 = 7,
    DECLINE_603 = 8,
    CANCEL = 9,
};

struct SigMessage {
    SigMethod method = SigMethod::INVITE;
    uint32_t call_id = 0;               // caller-chosen dialog key
    MediaKind media = MediaKind::VOICE; // meaningful on INVITE
    uint32_t conf_id = 0;               // M18: != 0 -> conference dialog
};

std::vector<uint8_t> encode_sig(const SigMessage& msg);
bool decode_sig(const std::vector<uint8_t>& payload, SigMessage& out);

struct U2uPacket {
    MediaKind kind = MediaKind::MSG;
    bool ack = false;
    bool end = false;
    uint32_t seq = 0;
    uint32_t timestamp_ms = 0;
    std::string src_imsi;
    std::string dst_imsi; // empty for conference media (the BS bridge fans out)
    uint32_t conf_id = 0; // M18: != 0 -> conference packet (CONF flag on wire)
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> encode_u2u(const U2uPacket& pkt);
// Strict parse: fails unless the magic, kind nibble and IMSI length fields
// are all consistent, so legacy loopback SDUs never alias as U2U packets.
bool decode_u2u(const std::vector<uint8_t>& data, U2uPacket& out);

// Traffic profiles sized to what the PHY/MAC budget sustains at 5% burst
// loss (voice SDU stays under the RLC AM segmentation threshold; video
// segments into a few AM PDUs per packet). Rates are bounded by the
// per-flow HARQ window throughput (~35-55 blocks/s loaded), not by CPU.
struct MediaProfile {
    size_t payload_bytes;
    uint32_t interval_ms;
};
MediaProfile media_profile(MediaKind kind); // MSG -> {0, 0} (one-shot)

// Deterministic synthetic payload (a pseudo frame of "media").
std::vector<uint8_t> make_media_payload(MediaKind kind, uint32_t seq);

}

#endif // AETHER_APP_U2U_H
