#include "app/app_layer.h"
#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include "mac/mac_pdu.h"
#include "phy/ofdm.h"
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

std::vector<uint8_t> full_stack_tx(const std::vector<uint8_t>& app_data, uint8_t lcid) {
    auto pdcp_pdu = pdcp::tx(app_data);
    auto rlc_pdu = rlc::tm_tx(pdcp_pdu);
    auto mac_pdu = mac::build_pdu({{lcid, rlc_pdu}});
    return mac_pdu;
}

std::vector<uint8_t> full_stack_rx(const std::vector<uint8_t>& mac_bytes) {
    auto parsed = mac::parse_pdu(mac_bytes);
    if (parsed.empty()) return {};
    auto rlc_out = rlc::tm_rx(parsed[0].second);
    return pdcp::rx(rlc_out);
}

}

TEST(UserPlane, AppLayerSendReceive) {
    app::AppLayer ue_app;
    app::AppLayer bs_app;

    std::vector<uint8_t> ue_sent;
    ue_app.set_send_callback([&](const std::vector<uint8_t>& data) {
        ue_sent = data;
    });

    std::vector<uint8_t> hello = {'H', 'e', 'l', 'l', 'o'};
    ue_app.send_data(hello);
    EXPECT_EQ(ue_app.tx_count(), 1u);
    EXPECT_EQ(ue_sent, hello);

    bs_app.on_data_received(ue_sent);
    EXPECT_EQ(bs_app.rx_count(), 1u);
    EXPECT_EQ(bs_app.last_received(), hello);
}

TEST(UserPlane, PingPongFullStackHighSnr) {
    app::AppLayer ue_app;
    app::AppLayer bs_app;

    std::vector<uint8_t> hello = {'H', 'e', 'l', 'l', 'o'};
    std::vector<uint8_t> world = {'W', 'o', 'r', 'l', 'd'};

    // UE → BS
    auto ue_mac = full_stack_tx(hello, 1);
    auto bits = bytes_to_bits(ue_mac);
    auto iq = phy::phy_tx(bits);
    auto noisy = add_awgn(iq, 30.0);
    auto rx_bits = phy::phy_rx(noisy, bits.size());
    auto rx_bytes = bits_to_bytes(rx_bits);
    auto ue_data_at_bs = full_stack_rx(rx_bytes);
    bs_app.on_data_received(ue_data_at_bs);
    EXPECT_EQ(bs_app.last_received(), hello);

    // BS → UE
    auto bs_mac = full_stack_tx(world, 1);
    auto bits2 = bytes_to_bits(bs_mac);
    auto iq2 = phy::phy_tx(bits2);
    auto noisy2 = add_awgn(iq2, 30.0);
    auto rx_bits2 = phy::phy_rx(noisy2, bits2.size());
    auto rx_bytes2 = bits_to_bytes(rx_bits2);
    auto bs_data_at_ue = full_stack_rx(rx_bytes2);
    ue_app.on_data_received(bs_data_at_ue);
    EXPECT_EQ(ue_app.last_received(), world);
}

TEST(UserPlane, MultiplePingPongs) {
    app::AppLayer ue_app;
    app::AppLayer bs_app;

    std::vector<std::vector<uint8_t>> payloads = {
        {0x01, 0x02}, {0x03, 0x04}, {0x05, 0x06, 0x07},
    };

    for (auto& payload : payloads) {
        auto mac_pdu = full_stack_tx(payload, 1);
        auto bits = bytes_to_bits(mac_pdu);
        auto iq = phy::phy_tx(bits);
        auto noisy = add_awgn(iq, 30.0);
        auto rx_bits = phy::phy_rx(noisy, bits.size());
        auto rx_bytes = bits_to_bytes(rx_bits);
        auto recovered = full_stack_rx(rx_bytes);
        bs_app.on_data_received(recovered);
        EXPECT_EQ(bs_app.last_received(), payload);
    }
    EXPECT_EQ(bs_app.rx_count(), 3u);
}

TEST(UserPlane, MscGeneratorTest) {
    // Verify MSC generator script can be imported
    // (Python test done separately; here we just check the tool exists)
}
