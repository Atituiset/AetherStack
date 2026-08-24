#ifndef AETHER_CORE_RADIO_FRAMES_H
#define AETHER_CORE_RADIO_FRAMES_H

#include <cstdint>
#include <vector>

namespace core {

// Air-frame types carried in the bit domain after PHY decode.
// Wire layout: [type:1][rnti:2 LE][payload...]
enum class AirFrameType : uint8_t {
    MSG1_PRACH = 0xA1,
    MSG2_RAR = 0xA2,
    MSG3_CCCH = 0xA3,
    MSG4_CR = 0xA4,
    DATA = 0xA5,
};

struct AirFrame {
    AirFrameType type = AirFrameType::DATA;
    uint16_t rnti = 0; // DATA/MSG3 association id; 0 when unused
    std::vector<uint8_t> payload;
};

// Serialize an AirFrame to bytes.
std::vector<uint8_t> encode_frame(const AirFrame& frame);

// Parse bytes into an AirFrame. Returns false on truncation or unknown type.
bool decode_frame(const uint8_t* data, size_t len, AirFrame& out);

// QPSK needs an even bit count; zero-pad to even.
std::vector<uint8_t> pad_bits_to_even(const std::vector<uint8_t>& bits);

// Bit-domain framing for the PHY chain. phy_rx_auto() decodes every OFDM
// symbol in the burst, including zero padding, so the byte length is carried
// as a 16-bit little-endian prefix followed by LSB-first byte bits.
//
//   [len:16 LE][payload bits...][zero padding to even]
std::vector<uint8_t> pack_air_bits(const std::vector<uint8_t>& payload);

// Extract the framed payload from decoded bits. Returns false when the
// stream is too short or the declared length exceeds the available bits.
bool unpack_air_bits(const std::vector<uint8_t>& bits, std::vector<uint8_t>& out);

}

#endif
