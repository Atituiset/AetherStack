#include "phy/ofdm.h"
#include "phy/phy_io.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace phy;

TEST(PhyIo, BitsRoundTrip) {
    std::vector<uint8_t> bits = {1, 0, 1, 1, 0, 0, 1, 0, 1};
    auto bytes = bits_to_bytes(bits);
    auto out = bytes_to_bits(bytes.data(), bytes.size());
    EXPECT_EQ(out, bits);
}

TEST(PhyIo, BytesToBitsRejectsTruncatedBuffer) {
    // Header claims 100 bits but only 4 header bytes present.
    std::vector<uint8_t> bytes = {100, 0, 0, 0};
    EXPECT_TRUE(bytes_to_bits(bytes.data(), bytes.size()).empty());

    // Header claims 100 bits but body holds only 2 of the needed 13 bytes.
    std::vector<uint8_t> short_body = {100, 0, 0, 0, 0xFF, 0xFF};
    EXPECT_TRUE(bytes_to_bits(short_body.data(), short_body.size()).empty());
}

TEST(PhyIo, BytesToBitsRejectsShortHeader) {
    uint8_t buf[3] = {0x08, 0x00, 0x00};
    EXPECT_TRUE(bytes_to_bits(buf, 3).empty());
    EXPECT_TRUE(bytes_to_bits(nullptr, 0).empty());
}

TEST(PhyIo, BytesToIqRejectsTruncated) {
    uint32_t count = 64;
    std::vector<uint8_t> bytes(sizeof(uint32_t));
    std::memcpy(bytes.data(), &count, sizeof(uint32_t));
    bytes.push_back(0x00); // far fewer floats than claimed
    EXPECT_TRUE(bytes_to_iq(bytes.data(), bytes.size()).empty());
}

TEST(PhyIo, AutoLengthRoundTripThroughPhyChain) {
    // phy_rx_auto() decodes every OFDM symbol (including padding), so the
    // decoded stream is a multiple of the subcarrier count; the caller frames
    // payloads on top of it. Here we assert exact bit recovery for the
    // modulated prefix.
    std::vector<uint8_t> bits = {1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0};
    auto iq = phy_tx(bits);
    auto decoded = phy_rx_auto(iq);
    ASSERT_GE(decoded.size(), bits.size());
    EXPECT_EQ(std::vector<uint8_t>(decoded.begin(), decoded.begin() + bits.size()), bits);
}
