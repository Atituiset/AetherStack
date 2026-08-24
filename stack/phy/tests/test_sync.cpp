// M10: cell search, synchronisation and channel-estimation tests.
#include "phy/frame.h"
#include "phy/sync.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>

using namespace phy;

namespace {

std::vector<cfloat> add_awgn(const std::vector<cfloat>& in, double snr_db,
                             std::mt19937& rng) {
    double p = 0;
    for (const auto& x : in) p += std::norm(x);
    p /= static_cast<double>(in.size());
    double np = p / std::pow(10.0, snr_db / 10.0);
    double sigma = std::sqrt(np / 2.0);
    std::normal_distribution<float> g(0.f, static_cast<float>(sigma));
    std::vector<cfloat> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] + cfloat(g(rng), g(rng));
    return out;
}

} // namespace

TEST(Zc, ConstantAmplitudeAutoCorrelation) {
    auto z = zadoff_chu(25, 63);
    for (const auto& x : z) {
        EXPECT_NEAR(std::abs(x), 1.0f, 1e-5f); // CAZAC: constant modulus
    }
}

TEST(Sync, DetectsTimingAndNid2CleanChannel) {
    const int n_fft = 64, cp = 16;
    FrameTxConfig cfg{ n_fft, cp, make_pci(1, 2) };
    auto burst = phy_tx_frame(std::vector<uint8_t>(frame_bits_capacity(n_fft) / 2, 1),
                              cfg);

    auto sync = pss_detect(burst, n_fft, cp);
    ASSERT_TRUE(sync.found);
    EXPECT_EQ(sync.nid2_hint, 2);
    // Peak lands on the post-CP PSS body.
    EXPECT_EQ(sync.timing, cp);
}

TEST(Sync, DetectsUnderNoiseAndOffset) {
    const int n_fft = 64, cp = 16;
    FrameTxConfig cfg{ n_fft, cp, make_pci(0, 1) };
    std::mt19937 rng(3);
    std::vector<uint8_t> bits(frame_bits_capacity(n_fft) / 2);
    for (auto& b : bits) b = rng() & 1;
    auto burst = phy_tx_frame(bits, cfg);

    // Prepend random garbage to offset the true start.
    std::normal_distribution<float> g(0.f, 1.f);
    std::vector<cfloat> rx(37);
    for (auto& x : rx) x = cfloat(g(rng), g(rng));
    rx.insert(rx.end(), burst.begin(), burst.end());

    auto noisy = add_awgn(rx, 15.0, rng);
    auto sync = pss_detect(noisy, n_fft, cp);
    ASSERT_TRUE(sync.found);
    EXPECT_EQ(sync.nid2_hint, 1);
    EXPECT_EQ(sync.timing, 37 + cp);
}

TEST(Frame, CleanRoundTripPreservesBitsAndPci) {
    const int n_fft = 64, cp = 16;
    FrameTxConfig cfg{ n_fft, cp, make_pci(1, 0) };
    std::mt19937 rng(8);
    std::vector<uint8_t> bits(frame_bits_capacity(n_fft) / 2);
    for (auto& b : bits) b = rng() & 1;

    auto tx = phy_tx_frame(bits, cfg);
    FrameRxResult res;
    auto rx = phy_rx_frame(tx, cfg, res);

    ASSERT_TRUE(res.synced);
    ASSERT_TRUE(res.pci_confirmed);
    EXPECT_EQ(res.pci, cfg.pci);
    if (rx != bits) {
        for (int s = 0; s < 4; ++s) {
            printf("[diag] sym%d tx:", s);
            for (int b = 0; b < 8; ++b)
                printf(" %d", bits[s * 8 + b]);
            printf(" | rx:");
            for (int b = 0; b < 8; ++b)
                printf(" %d", rx[s * 8 + b]);
            printf("\n");
        }
    }
    EXPECT_EQ(rx, bits);
}

TEST(Frame, ChannelEstimateEqualisesMultipathTaps) {
    // Two-tap channel: [1.0, -0.45 @ 3 samples]. Without equalisation the
    // frequency-selective fading would shred the QPSK grid.
    const int n_fft = 64, cp = 16;
    FrameTxConfig cfg{ n_fft, cp, make_pci(0, 0) };
    std::mt19937 rng(21);
    std::vector<uint8_t> bits(frame_data_symbols(600, n_fft) * n_fft * 2);
    for (auto& b : bits) b = rng() & 1;

    auto tx = phy_tx_frame(bits, cfg);
    std::vector<cfloat> ch(tx.size(), {0, 0});
    for (size_t i = 0; i < tx.size(); ++i) {
        ch[i] = tx[i];
        if (i >= 3) ch[i] += -0.45f * tx[i - 3];
    }

    FrameRxResult res;
    auto rx = phy_rx_frame(ch, cfg, res);
    ASSERT_TRUE(res.pci_confirmed);

    size_t errs = 0;
    for (size_t i = 0; i < bits.size(); ++i) errs += (rx[i] != bits[i]);
    // A couple of edge subcarriers may survive marginally; demand >99%.
    EXPECT_LT(errs * 100.0 / bits.size(), 1.0)
        << "bit error rate too high under multipath";
    if (errs > 0) {
        printf("[diag] first mismatch at %zu\n",
               [&]{ for (size_t i=0;i<bits.size();++i) if (rx[i]!=bits[i]) return i; return (size_t)-1; }());
    }
}

TEST(Fft, InverseForwardRoundTrip) {
    std::mt19937 rng(77);
    std::normal_distribution<float> g(0.f, 1.f);
    std::vector<cfloat> orig(64);
    for (auto& x : orig) x = cfloat(g(rng), g(rng));
    auto work = orig;
    fft_inplace(work, true);
    fft_inplace(work, false);
    double err = 0;
    for (size_t i = 0; i < orig.size(); ++i) {
        err += std::abs(work[i] - orig[i]);
    }
    EXPECT_LT(err / orig.size(), 1e-4);
}

TEST(Frame, PreamblePlusDataBurstRoundTrip) {
    // Mirrors the real radio adapter: preamble burst + modulated data in
    // one sample stream.
    const int n_fft = 64, cp = 16;
    FrameTxConfig cfg{ n_fft, cp, make_pci(1, 0) };
    std::mt19937 rng(99);
    std::vector<uint8_t> bits(frame_data_symbols(600, n_fft) * n_fft * 2);
    for (auto& b : bits) b = rng() & 1;

    auto pre = phy_preamble_burst(cfg);
    auto data_iq = phy_tx_data(bits, cfg);
    std::vector<cfloat> all = pre;
    all.insert(all.end(), data_iq.begin(), data_iq.end());

    FrameRxResult res;
    auto rx = phy_rx_frame(all, cfg, res);
    ASSERT_TRUE(res.synced);
    ASSERT_TRUE(res.pci_confirmed);
    ASSERT_GE(rx.size(), bits.size());
    rx.resize(bits.size());
    EXPECT_EQ(rx, bits);
}
