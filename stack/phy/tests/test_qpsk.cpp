#include "phy/qpsk.h"
#include "phy/phy_common.h"
#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

using namespace phy;

TEST(QpskModulate, AllZeroBits) {
    std::vector<uint8_t> bits = {0, 0};
    auto syms = qpsk_modulate(bits);
    ASSERT_EQ(syms.size(), 1u);
    float s = static_cast<float>(QPSK_NORM);
    EXPECT_NEAR(syms[0].real(), s, 1e-6f);
    EXPECT_NEAR(syms[0].imag(), s, 1e-6f);
}

TEST(QpskModulate, AllOneBits) {
    std::vector<uint8_t> bits = {1, 1};
    auto syms = qpsk_modulate(bits);
    ASSERT_EQ(syms.size(), 1u);
    float s = static_cast<float>(QPSK_NORM);
    EXPECT_NEAR(syms[0].real(), -s, 1e-6f);
    EXPECT_NEAR(syms[0].imag(), -s, 1e-6f);
}

TEST(QpskModulate, AllFourConstellationPoints) {
    float s = static_cast<float>(QPSK_NORM);
    struct TestCase { uint8_t b0, b1; float expected_re, expected_im; };
    std::vector<TestCase> cases = {
        {0, 0,  s,  s},
        {0, 1,  s, -s},
        {1, 0, -s,  s},
        {1, 1, -s, -s},
    };
    for (const auto& tc : cases) {
        std::vector<uint8_t> bits = {tc.b0, tc.b1};
        auto syms = qpsk_modulate(bits);
        ASSERT_EQ(syms.size(), 1u);
        EXPECT_NEAR(syms[0].real(), tc.expected_re, 1e-6f)
            << "for bits {" << (int)tc.b0 << "," << (int)tc.b1 << "}";
        EXPECT_NEAR(syms[0].imag(), tc.expected_im, 1e-6f)
            << "for bits {" << (int)tc.b0 << "," << (int)tc.b1 << "}";
    }
}

TEST(QpskModulate, OddBitCountReturnsEmpty) {
    std::vector<uint8_t> bits = {0, 1, 1};
    auto syms = qpsk_modulate(bits);
    EXPECT_TRUE(syms.empty());
}

TEST(QpskModulate, AveragePower) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 1);
    size_t n_bits = 2000;
    std::vector<uint8_t> bits(n_bits);
    for (auto& b : bits) b = dist(rng);
    auto syms = qpsk_modulate(bits);
    double power = 0;
    for (const auto& s : syms) {
        power += std::norm(s);
    }
    power /= syms.size();
    EXPECT_NEAR(power, 1.0, 0.01);
}

TEST(QpskDemodulate, RoundTripNoNoise) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 1);
    for (int trial = 0; trial < 10; ++trial) {
        size_t n_bits = 128;
        std::vector<uint8_t> bits(n_bits);
        for (auto& b : bits) b = dist(rng);
        auto syms = qpsk_modulate(bits);
        auto recovered = qpsk_demodulate(syms);
        ASSERT_EQ(recovered.size(), bits.size());
        for (size_t i = 0; i < bits.size(); ++i) {
            EXPECT_EQ(recovered[i], bits[i]) << "mismatch at bit " << i;
        }
    }
}

TEST(QpskDemodulate, NoisyRoundTrip) {
    std::mt19937 rng(99);
    std::uniform_int_distribution<int> bit_dist(0, 1);
    std::normal_distribution<float> noise_dist(0.0f, 0.05f);
    size_t n_bits = 1000;
    std::vector<uint8_t> bits(n_bits);
    for (auto& b : bits) b = bit_dist(rng);
    auto syms = qpsk_modulate(bits);
    for (auto& s : syms) {
        s += std::complex<float>(noise_dist(rng), noise_dist(rng));
    }
    auto recovered = qpsk_demodulate(syms);
    int errors = 0;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (recovered[i] != bits[i]) ++errors;
    }
    float ber = static_cast<float>(errors) / bits.size();
    EXPECT_LT(ber, 0.05f);
}
