#include "app/u2u.h"

namespace app {

const char* media_kind_name(MediaKind kind) {
    switch (kind) {
        case MediaKind::MSG: return "msg";
        case MediaKind::VOICE: return "voice";
        case MediaKind::VIDEO: return "video";
        case MediaKind::SIG: return "sig";
    }
    return "msg";
}

std::vector<uint8_t> encode_sig(const SigMessage& msg) {
    std::vector<uint8_t> out(6);
    out[0] = static_cast<uint8_t>(msg.method);
    for (int i = 0; i < 4; ++i) {
        out[1 + i] = static_cast<uint8_t>((msg.call_id >> (8 * i)) & 0xFF);
    }
    out[5] = static_cast<uint8_t>(msg.media);
    if (msg.conf_id != 0) { // M18: trailing conf_id marks a conference dialog
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>((msg.conf_id >> (8 * i)) & 0xFF));
        }
    }
    return out;
}

bool decode_sig(const std::vector<uint8_t>& payload, SigMessage& out) {
    if (payload.size() < 6) return false;
    const uint8_t method = payload[0];
    if (method < static_cast<uint8_t>(SigMethod::INVITE) ||
        method > static_cast<uint8_t>(SigMethod::CANCEL)) {
        return false; // unknown method: not a sig payload we understand
    }
    out.method = static_cast<SigMethod>(method);
    out.call_id = 0;
    for (int i = 0; i < 4; ++i) {
        out.call_id |= static_cast<uint32_t>(payload[1 + i]) << (8 * i);
    }
    const uint8_t media = payload[5];
    out.media = media <= static_cast<uint8_t>(MediaKind::VIDEO)
                    ? static_cast<MediaKind>(media)
                    : MediaKind::VOICE;
    out.conf_id = 0;
    if (payload.size() >= 10) { // M18 trailing field; older peers send 6 B
        for (int i = 0; i < 4; ++i) {
            out.conf_id |= static_cast<uint32_t>(payload[6 + i]) << (8 * i);
        }
    }
    return true;
}

std::vector<uint8_t> encode_u2u(const U2uPacket& pkt) {
    std::vector<uint8_t> out;
    out.reserve(12 + pkt.src_imsi.size() + pkt.dst_imsi.size() +
                pkt.payload.size());
    out.push_back(kU2uMagic);
    uint8_t kind = static_cast<uint8_t>(pkt.kind) & 0x0F;
    if (pkt.conf_id != 0) kind |= kU2uFlagConf;
    if (pkt.end) kind |= kU2uFlagEnd;
    if (pkt.ack) kind |= kU2uFlagAck;
    out.push_back(kind);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((pkt.seq >> (8 * i)) & 0xFF));
    }
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((pkt.timestamp_ms >> (8 * i)) & 0xFF));
    }
    out.push_back(static_cast<uint8_t>(pkt.src_imsi.size()));
    out.insert(out.end(), pkt.src_imsi.begin(), pkt.src_imsi.end());
    out.push_back(static_cast<uint8_t>(pkt.dst_imsi.size()));
    out.insert(out.end(), pkt.dst_imsi.begin(), pkt.dst_imsi.end());
    if (pkt.conf_id != 0) { // M18: [conf_id:4 LE] between dst and payload
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>((pkt.conf_id >> (8 * i)) & 0xFF));
        }
    }
    out.insert(out.end(), pkt.payload.begin(), pkt.payload.end());
    return out;
}

bool decode_u2u(const std::vector<uint8_t>& data, U2uPacket& out) {
    if (data.size() < 12 || data[0] != kU2uMagic) return false;
    const uint8_t kind_byte = data[1];
    const uint8_t kind_nibble = kind_byte & 0x0F;
    if (kind_nibble > static_cast<uint8_t>(MediaKind::SIG)) return false;
    const bool has_conf = (kind_byte & kU2uFlagConf) != 0;
    const size_t src_len = data[10];
    if (src_len == 0 || src_len > kU2uMaxImsiLen) return false;
    if (data.size() < 12 + src_len) return false;
    const size_t dst_len = data[11 + src_len];
    // Conference media is addressed to the bridge (empty dst); everything
    // else must name a destination.
    if (dst_len == 0 && !has_conf) return false;
    if (dst_len > kU2uMaxImsiLen) return false;
    if (data.size() < 12 + src_len + dst_len + (has_conf ? 4 : 0)) return false;

    out.kind = static_cast<MediaKind>(kind_nibble);
    out.end = (kind_byte & kU2uFlagEnd) != 0;
    out.ack = (kind_byte & kU2uFlagAck) != 0;
    out.seq = 0;
    for (int i = 0; i < 4; ++i) {
        out.seq |= static_cast<uint32_t>(data[2 + i]) << (8 * i);
    }
    out.timestamp_ms = 0;
    for (int i = 0; i < 4; ++i) {
        out.timestamp_ms |= static_cast<uint32_t>(data[6 + i]) << (8 * i);
    }
    out.src_imsi.assign(data.begin() + 11, data.begin() + 11 + src_len);
    out.dst_imsi.assign(data.begin() + 12 + src_len,
                        data.begin() + 12 + src_len + dst_len);
    out.conf_id = 0;
    size_t payload_off = 12 + src_len + dst_len;
    if (has_conf) {
        for (int i = 0; i < 4; ++i) {
            out.conf_id |= static_cast<uint32_t>(data[payload_off + i])
                           << (8 * i);
        }
        payload_off += 4;
    }
    out.payload.assign(data.begin() + payload_off, data.end());
    return true;
}

MediaProfile media_profile(MediaKind kind) {
    switch (kind) {
        // 33 pkt/s each way (media + acks): the per-flow HARQ window
        // sustains ~35-55 blocks/s under load at 5% loss, and 50 pkt/s
        // ran the pipes at >95% occupancy (RTT drift, ~10% gap loss).
        case MediaKind::VOICE: return {160, 30};  // ~43 kbit/s, one AM PDU
        // 2 AM PDUs per packet: the per-flow DL HARQ budget sustains ~50
        // blocks/s at 5% loss, so video targets ~40 blocks/s each way.
        case MediaKind::VIDEO: return {512, 50};  // ~82 kbit/s, two AM PDUs
        case MediaKind::MSG: return {0, 0};       // one-shot
    }
    return {0, 0};
}

std::vector<uint8_t> make_media_payload(MediaKind kind, uint32_t seq) {
    const MediaProfile p = media_profile(kind);
    std::vector<uint8_t> payload(p.payload_bytes);
    // Deterministic pseudo-media: byte pattern depends on kind and seq so
    // PDU_TRACE output looks like a real stream.
    const uint8_t base = kind == MediaKind::VIDEO ? 0x30 : 0x60;
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(base + ((i + seq * 7) % 26));
    }
    return payload;
}

}

// U2U 媒体帧入口：先校验帧长与类型，再按媒体类型分发
int u2u_media_src_len_checked(MediaKind kind, const uint8_t *data, size_t size) {
    if (size < 12 || kind == MediaKind::SIG)
        return -1;
    return app::media_src_len(kind, data);
}
