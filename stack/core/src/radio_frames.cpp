#include "core/radio_frames.h"

namespace core {

std::vector<uint8_t> encode_frame(const AirFrame& frame) {
    std::vector<uint8_t> bytes;
    bytes.reserve(3 + frame.payload.size());
    bytes.push_back(static_cast<uint8_t>(frame.type));
    bytes.push_back(static_cast<uint8_t>(frame.rnti & 0xFF));
    bytes.push_back(static_cast<uint8_t>((frame.rnti >> 8) & 0xFF));
    bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
    return bytes;
}

bool decode_frame(const uint8_t* data, size_t len, AirFrame& out) {
    if (data == nullptr || len < 3) return false;
    switch (data[0]) {
        case static_cast<uint8_t>(AirFrameType::MSG1_PRACH):
        case static_cast<uint8_t>(AirFrameType::MSG2_RAR):
        case static_cast<uint8_t>(AirFrameType::MSG3_CCCH):
        case static_cast<uint8_t>(AirFrameType::MSG4_CR):
        case static_cast<uint8_t>(AirFrameType::DATA):
            break;
        default:
            return false;
    }
    out.type = static_cast<AirFrameType>(data[0]);
    out.rnti = static_cast<uint16_t>(data[1] | (data[2] << 8));
    out.payload.assign(data + 3, data + len);
    return true;
}

std::vector<uint8_t> pad_bits_to_even(const std::vector<uint8_t>& bits) {
    if (bits.size() % 2 == 0) return bits;
    std::vector<uint8_t> padded(bits);
    padded.push_back(0);
    return padded;
}

namespace {

// Append one byte LSB-first (matches the phy_io packing convention).
void append_byte_bits(std::vector<uint8_t>& bits, uint8_t value) {
    for (int i = 0; i < 8; ++i) {
        bits.push_back((value >> i) & 1);
    }
}

} // namespace

std::vector<uint8_t> pack_air_bits(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> bits;
    bits.reserve(16 + payload.size() * 8 + 1);
    uint16_t len = static_cast<uint16_t>(payload.size());
    append_byte_bits(bits, static_cast<uint8_t>(len & 0xFF));
    append_byte_bits(bits, static_cast<uint8_t>((len >> 8) & 0xFF));
    for (uint8_t b : payload) {
        append_byte_bits(bits, b);
    }
    return pad_bits_to_even(bits);
}

bool unpack_air_bits(const std::vector<uint8_t>& bits, std::vector<uint8_t>& out) {
    if (bits.size() < 16) return false;
    auto read_byte = [&bits](size_t bit_offset) {
        uint8_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<uint8_t>((bits[bit_offset + i] & 1) << i);
        }
        return value;
    };
    size_t len = read_byte(0) | (static_cast<size_t>(read_byte(8)) << 8);
    if (16 + len * 8 > bits.size()) return false;
    out.clear();
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(read_byte(16 + i * 8));
    }
    return true;
}

}
