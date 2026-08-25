#include "pdcp/pdcp_entity.h"
#include "common/crypto.h"
#include "rlc/rlc_tm.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
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

TEST(PdcpSecurity, ProtectedFrameHidesPlaintext) {
    auto key = crypto::to_bytes("0123456789abcdef0123456789abcdef");
    std::array<uint8_t, crypto::kKey256Size> k{};
    std::copy(key.begin(), key.end(), k.begin());
    std::vector<uint8_t> secret = crypto::to_bytes("TOPSECRET-PAYLOAD-TOPSECRET");
    auto framed = pdcp::protect(k, /*seq=*/7, secret);
    ASSERT_EQ(framed[0] & 0x01, 0x01);
    // ciphertext must not contain the plaintext byte pattern anywhere
    EXPECT_TRUE(std::search(framed.begin(), framed.end(), secret.begin(),
                            secret.end()) == framed.end());
    std::vector<uint8_t> out;
    ASSERT_TRUE(pdcp::unprotect(k, framed, out));
    EXPECT_EQ(out, secret);
    // wrong key must NOT restore the plaintext
    std::array<uint8_t, crypto::kKey256Size> bad{};
    bad.fill(0x11);
    std::vector<uint8_t> out2;
    ASSERT_TRUE(pdcp::unprotect(bad, framed, out2));
    EXPECT_NE(out2, secret);
}
