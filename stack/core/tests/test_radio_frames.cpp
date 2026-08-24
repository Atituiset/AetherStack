#include "core/radio_frames.h"
#include <gtest/gtest.h>
#include <vector>

using namespace core;

TEST(RadioFrames, EncodeDecodeDataRoundTrip) {
    AirFrame f;
    f.type = AirFrameType::DATA;
    f.rnti = 0x1234;
    f.payload = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto bytes = encode_frame(f);
    ASSERT_EQ(bytes.size(), 3u + f.payload.size());

    AirFrame out;
    ASSERT_TRUE(decode_frame(bytes.data(), bytes.size(), out));
    EXPECT_EQ(out.type, AirFrameType::DATA);
    EXPECT_EQ(out.rnti, 0x1234);
    EXPECT_EQ(out.payload, f.payload);
}

TEST(RadioFrames, EncodeDecodeMsg1ZeroRnti) {
    AirFrame f;
    f.type = AirFrameType::MSG1_PRACH;
    f.rnti = 0;
    f.payload = {42};

    auto bytes = encode_frame(f);
    AirFrame out;
    ASSERT_TRUE(decode_frame(bytes.data(), bytes.size(), out));
    EXPECT_EQ(out.type, AirFrameType::MSG1_PRACH);
    EXPECT_EQ(out.rnti, 0u);
    EXPECT_EQ(out.payload, std::vector<uint8_t>{42});
}

TEST(RadioFrames, DecodeRejectsTooShort) {
    AirFrame out;
    uint8_t buf[2] = {0xA5, 0x00};
    EXPECT_FALSE(decode_frame(buf, 2, out));
    EXPECT_FALSE(decode_frame(nullptr, 0, out));
}

TEST(RadioFrames, DecodeRejectsUnknownType) {
    std::vector<uint8_t> bytes = {0x99, 0x00, 0x00, 0x01};
    AirFrame out;
    EXPECT_FALSE(decode_frame(bytes.data(), bytes.size(), out));
}

TEST(RadioFrames, BitsPadToEven) {
    EXPECT_EQ(pad_bits_to_even({1}), (std::vector<uint8_t>{1, 0}));
    EXPECT_EQ(pad_bits_to_even({1, 0, 1, 1}), (std::vector<uint8_t>{1, 0, 1, 1}));
    EXPECT_TRUE(pad_bits_to_even({}).empty());
}

TEST(RadioFrames, AirBitsRoundTripExactPayload) {
    std::vector<uint8_t> payload = {0xA5, 0x34, 0x12, 0xDE, 0xAD};
    auto bits = pack_air_bits(payload);
    // Packed stream always carries an even number of bits (QPSK requirement).
    EXPECT_EQ(bits.size() % 2, 0u);

    std::vector<uint8_t> out;
    ASSERT_TRUE(unpack_air_bits(bits, out));
    EXPECT_EQ(out, payload);
}

TEST(RadioFrames, AirBitsIgnoreTrailingPadding) {
    std::vector<uint8_t> payload = {0x01};
    auto bits = pack_air_bits(payload);
    // Simulate PHY decoding extra zero-padded symbols beyond our frame.
    std::vector<uint8_t> with_noise_tail(bits);
    with_noise_tail.insert(with_noise_tail.end(), {0, 0, 0, 0, 0, 0, 0, 0});

    std::vector<uint8_t> out;
    ASSERT_TRUE(unpack_air_bits(with_noise_tail, out));
    EXPECT_EQ(out, payload);
}

TEST(RadioFrames, AirBitsRejectTooShortStream) {
    std::vector<uint8_t> out;
    EXPECT_FALSE(unpack_air_bits({}, out));
    std::vector<uint8_t> few = {1, 0, 1}; // shorter than the 16-bit header
    EXPECT_FALSE(unpack_air_bits(few, out));
}

TEST(RadioFrames, AirBitsRejectLengthBeyondStream) {
    // Header claims 100 bytes but stream holds far fewer bits.
    std::vector<uint8_t> lie = {
        100, 0, // little-endian 16-bit length = 100
        1, 0, 1, 0, 1, 0, 1, 0,
    };
    std::vector<uint8_t> out;
    EXPECT_FALSE(unpack_air_bits(lie, out));
}
