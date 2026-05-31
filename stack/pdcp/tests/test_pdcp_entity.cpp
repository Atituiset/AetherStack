#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include <gtest/gtest.h>
#include <vector>

using namespace pdcp;

TEST(PdcpEntity, RoundTripPreservesData) {
    std::vector<uint8_t> sdu = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    auto pdu = tx(sdu);
    auto result = rx(pdu);
    EXPECT_EQ(result, sdu);
}

TEST(PdcpEntity, TxAddsTwoByteHeader) {
    std::vector<uint8_t> sdu = {0x01, 0x02, 0x03};
    auto pdu = tx(sdu);
    EXPECT_EQ(pdu.size(), sdu.size() + PDCP_HEADER_SIZE);
}

TEST(PdcpEntity, EmptySduTxProducesHeaderOnly) {
    std::vector<uint8_t> sdu;
    auto pdu = tx(sdu);
    EXPECT_EQ(pdu.size(), PDCP_HEADER_SIZE);
    auto result = rx(pdu);
    EXPECT_TRUE(result.empty());
}

TEST(PdcpEntity, RxWithShortInputReturnsEmpty) {
    std::vector<uint8_t> pdu = {0x00};
    auto result = rx(pdu);
    EXPECT_TRUE(result.empty());
}

TEST(PdcpEntity, PdcpRlcVerticalIntegration) {
    std::vector<uint8_t> original = {0xAA, 0xBB, 0xCC};
    auto pdcp_pdu = tx(original);
    auto rlc_sdu = rlc::tm_tx(pdcp_pdu);
    auto rlc_out = rlc::tm_rx(rlc_sdu);
    auto final_sdu = rx(rlc_out);
    EXPECT_EQ(final_sdu, original);
}
