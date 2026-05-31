#include "phy/ofdm.h"
#include "phy/qpsk.h"
#include "phy/phy_common.h"
#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

using namespace phy;

class OfdmTest : public ::testing::Test {
protected:
    void SetUp() override { rng.seed(42); }
    std::mt19937 rng;
};

TEST_F(OfdmTest, OfdmTxOutputLength) {
    int n_fft = 64, cp_len = 16;
    std::vector<std::complex<float>> symbols(n_fft, {1, 0});
    auto out = ofdm_tx(symbols, n_fft, cp_len);
    EXPECT_EQ(out.size(), static_cast<size_t>(n_fft + cp_len));
}

TEST_F(OfdmTest, OfdmTxMultipleSymbols) {
    int n_fft = 64, cp_len = 16;
    std::vector<std::complex<float>> symbols(n_fft * 3, {0.5f, -0.5f});
    auto out = ofdm_tx(symbols, n_fft, cp_len);
    EXPECT_EQ(out.size(), static_cast<size_t>(3 * (n_fft + cp_len)));
}

TEST_F(OfdmTest, OfdmRxEmptyInput) {
    auto out = ofdm_rx({}, 64, 16);
    EXPECT_TRUE(out.empty());
}

TEST_F(OfdmTest, OfdmRoundTripSingleSymbol) {
    int n_fft = 64, cp_len = 16;
    std::vector<std::complex<float>> symbols(n_fft);
    for (int i = 0; i < n_fft; ++i) {
        float phase = static_cast<float>(i) * 0.1f;
        symbols[i] = {std::cos(phase) * 0.5f, std::sin(phase) * 0.5f};
    }
    auto tx_out = ofdm_tx(symbols, n_fft, cp_len);
    auto rx_out = ofdm_rx(tx_out, n_fft, cp_len);
    ASSERT_EQ(rx_out.size(), symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        EXPECT_NEAR(rx_out[i].real(), symbols[i].real(), 1e-4f)
            << "real mismatch at subcarrier " << i;
        EXPECT_NEAR(rx_out[i].imag(), symbols[i].imag(), 1e-4f)
            << "imag mismatch at subcarrier " << i;
    }
}

TEST_F(OfdmTest, OfdmRoundTripMultipleSymbols) {
    int n_fft = 64, cp_len = 16;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<std::complex<float>> symbols(n_fft * 3);
    for (auto& s : symbols) s = {dist(rng), dist(rng)};
    auto tx_out = ofdm_tx(symbols, n_fft, cp_len);
    auto rx_out = ofdm_rx(tx_out, n_fft, cp_len);
    ASSERT_EQ(rx_out.size(), symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        EXPECT_NEAR(rx_out[i].real(), symbols[i].real(), 1e-3f);
        EXPECT_NEAR(rx_out[i].imag(), symbols[i].imag(), 1e-3f);
    }
}

TEST_F(OfdmTest, CyclicPrefixIsCorrect) {
    int n_fft = 64, cp_len = 16;
    std::vector<std::complex<float>> symbols(n_fft, {1, 0});
    auto tx_out = ofdm_tx(symbols, n_fft, cp_len);
    ASSERT_EQ(tx_out.size(), static_cast<size_t>(n_fft + cp_len));
    for (int i = 0; i < cp_len; ++i) {
        EXPECT_NEAR(tx_out[i].real(), tx_out[n_fft + i].real(), 1e-5f);
        EXPECT_NEAR(tx_out[i].imag(), tx_out[n_fft + i].imag(), 1e-5f);
    }
}

TEST_F(OfdmTest, PhyTxRxFullChainNoNoise) {
    int n_fft = 64, cp_len = 16;
    std::uniform_int_distribution<int> bit_dist(0, 1);
    size_t n_bits = 256;
    std::vector<uint8_t> bits(n_bits);
    for (auto& b : bits) b = bit_dist(rng);
    auto tx_samples = phy_tx(bits, n_fft, cp_len);
    auto rx_bits = phy_rx(tx_samples, n_bits, n_fft, cp_len);
    ASSERT_EQ(rx_bits.size(), bits.size());
    int errors = 0;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (rx_bits[i] != bits[i]) ++errors;
    }
    EXPECT_EQ(errors, 0);
}

TEST_F(OfdmTest, PhyTxRxFullChainWithLowNoise) {
    int n_fft = 64, cp_len = 16;
    std::uniform_int_distribution<int> bit_dist(0, 1);
    std::normal_distribution<float> noise_dist(0.0f, 0.01f);
    size_t n_bits = 512;
    std::vector<uint8_t> bits(n_bits);
    for (auto& b : bits) b = bit_dist(rng);
    auto tx_samples = phy_tx(bits, n_fft, cp_len);
    for (auto& s : tx_samples) {
        s += std::complex<float>(noise_dist(rng), noise_dist(rng));
    }
    auto rx_bits = phy_rx(tx_samples, n_bits, n_fft, cp_len);
    int errors = 0;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (rx_bits[i] != bits[i]) ++errors;
    }
    float ber = static_cast<float>(errors) / bits.size();
    EXPECT_LT(ber, 0.05f);
}
