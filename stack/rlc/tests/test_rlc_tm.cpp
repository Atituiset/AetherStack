#include "rlc/rlc_tm.h"
#include <gtest/gtest.h>
#include <vector>

using namespace rlc;

TEST(RlcTm, RoundTripPreservesData) {
    std::vector<uint8_t> sdu = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto pdu = tm_tx(sdu);
    auto result = tm_rx(pdu);
    EXPECT_EQ(result, sdu);
}

TEST(RlcTm, EmptySduReturnsEmptyPdu) {
    std::vector<uint8_t> sdu;
    auto pdu = tm_tx(sdu);
    EXPECT_TRUE(pdu.empty());
    auto result = tm_rx(pdu);
    EXPECT_TRUE(result.empty());
}

TEST(RlcTm, LargeSduRoundTrip) {
    std::vector<uint8_t> sdu(10000, 0xAB);
    auto pdu = tm_tx(sdu);
    EXPECT_EQ(pdu.size(), 10000u);
    auto result = tm_rx(pdu);
    EXPECT_EQ(result, sdu);
}

TEST(RlcTm, TxOutputEqualsInput) {
    std::vector<uint8_t> sdu = {0xFF, 0x00, 0xAA};
    auto pdu = tm_tx(sdu);
    EXPECT_EQ(pdu, sdu);
}
