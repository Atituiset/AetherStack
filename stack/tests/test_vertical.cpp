#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include "mac/mac_pdu.h"
#include "phy/ofdm.h"
#include "phy/qpsk.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>

namespace {

std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t byte : bytes) {
        for (int i = 7; i >= 0; --i) {
            bits.push_back((byte >> i) & 1);
        }
    }
    return bits;
}

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    if (bits.size() % 8 != 0) return bytes;
    bytes.reserve(bits.size() / 8);
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; ++j) {
            byte = (byte << 1) | bits[i + j];
        }
        bytes.push_back(byte);
    }
    return bytes;
}

std::vector<std::complex<float>> add_awgn(
    const std::vector<std::complex<float>>& samples, double snr_db) {
    double signal_power = 0.0;
    for (const auto& s : samples) {
        signal_power += std::norm(s);
    }
    signal_power /= samples.size();
    double snr_lin = std::pow(10.0, snr_db / 10.0);
    double noise_power = signal_power / snr_lin;
    double sigma = std::sqrt(noise_power / 2.0);

    std::mt19937 gen(42);
    std::normal_distribution<float> dist(0.0f, static_cast<float>(sigma));

    std::vector<std::complex<float>> noisy(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        noisy[i] = samples[i] + std::complex<float>(dist(gen), dist(gen));
    }
    return noisy;
}

}

TEST(VerticalPassthrough, PdcpRlcMacPhyRoundTripHighSnr) {
    std::vector<uint8_t> original_sdu = {0x48, 0x65, 0x6C, 0x6C, 0x6F};

    auto pdcp_pdu = pdcp::tx(original_sdu);
    auto rlc_pdu = rlc::tm_tx(pdcp_pdu);
    auto mac_pdu = mac::build_pdu({{1, rlc_pdu}});
    auto bits = bytes_to_bits(mac_pdu);
    auto iq_tx = phy::phy_tx(bits);

    auto iq_rx = add_awgn(iq_tx, 30.0);

    auto rx_bits = phy::phy_rx(iq_rx, bits.size());
    auto mac_rx = bits_to_bytes(rx_bits);
    auto parsed = mac::parse_pdu(mac_rx);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].first, 1);

    auto rlc_out = rlc::tm_rx(parsed[0].second);
    auto final_sdu = pdcp::rx(rlc_out);

    EXPECT_EQ(final_sdu, original_sdu);
}

TEST(VerticalPassthrough, PdcpRlcMacPhyRoundTripLargerPayload) {
    std::vector<uint8_t> original_sdu(50, 0x42);

    auto pdcp_pdu = pdcp::tx(original_sdu);
    auto rlc_pdu = rlc::tm_tx(pdcp_pdu);
    auto mac_pdu = mac::build_pdu({{3, rlc_pdu}});
    auto bits = bytes_to_bits(mac_pdu);
    auto iq_tx = phy::phy_tx(bits);

    auto iq_rx = add_awgn(iq_tx, 30.0);

    auto rx_bits = phy::phy_rx(iq_rx, bits.size());
    auto mac_rx = bits_to_bytes(rx_bits);
    auto parsed = mac::parse_pdu(mac_rx);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].first, 3);

    auto rlc_out = rlc::tm_rx(parsed[0].second);
    auto final_sdu = pdcp::rx(rlc_out);

    EXPECT_EQ(final_sdu, original_sdu);
}

TEST(VerticalPassthrough, PdcpRlcOnlyRoundTrip) {
    std::vector<uint8_t> original = {0xDE, 0xAD, 0xBE, 0xEF};
    auto pdcp_pdu = pdcp::tx(original);
    auto rlc_pdu = rlc::tm_tx(pdcp_pdu);
    auto rlc_out = rlc::tm_rx(rlc_pdu);
    auto final_sdu = pdcp::rx(rlc_out);
    EXPECT_EQ(final_sdu, original);
}

TEST(VerticalPassthrough, PduSizeIncrementsThroughStack) {
    std::vector<uint8_t> sdu = {0x01, 0x02, 0x03, 0x04};
    size_t sdu_len = sdu.size();

    auto pdcp_pdu = pdcp::tx(sdu);
    EXPECT_EQ(pdcp_pdu.size(), sdu_len + pdcp::PDCP_HEADER_SIZE);

    auto rlc_pdu = rlc::tm_tx(pdcp_pdu);
    EXPECT_EQ(rlc_pdu.size(), pdcp_pdu.size());

    auto mac_pdu = mac::build_pdu({{1, rlc_pdu}});
    EXPECT_GT(mac_pdu.size(), rlc_pdu.size());
}
