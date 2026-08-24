#ifndef AETHER_PHY_FRAME_H
#define AETHER_PHY_FRAME_H

// M10: structured PHY bursts with cell-search preamble and pilot-based
// channel estimation.
//
// Burst layout (per call):
//   [CP][PSS][CP][SSS][CP][DMRS][CP][data sym 0]...[data sym N-1]
//
// The receiver performs: PSS sliding correlation (timing + NID2), CP-based
// CFO estimation and compensation, FFT per symbol, PCI confirmation via
// SSS/DMRS correlation, LS channel estimation on the DMRS, and equalisation
// of the data symbols before QPSK demodulation.

#include "phy/phy_common.h"
#include <complex>
#include <cstdint>
#include <vector>

namespace phy {

using cfloat = std::complex<float>;

struct FrameTxConfig {
    int n_fft = DEFAULT_N_FFT;
    int cp_len = DEFAULT_CP_LEN;
    int pci = 0;
};

// Data OFDM symbols are allocated dynamically from the payload length;
// kFrameDataMaxSymbols caps a single burst.
constexpr int kFrameDataMaxSymbols = 12;

int frame_data_symbols(size_t n_bits, int n_fft);   // symbols needed
size_t frame_bits_capacity(int n_fft);              // max info bits

std::vector<cfloat> phy_tx_frame(const std::vector<uint8_t>& bits,
                                 const FrameTxConfig& cfg);

// Preamble-only burst (PSS+SSS+DMRS with CPs). Real radios prepend this to
// the modulated data symbols of every transmission so a receiver can sync,
// identify the cell and estimate the channel before touching the data.
std::vector<cfloat> phy_preamble_burst(const FrameTxConfig& cfg);

// Data symbols only (IFFT of the QPSK-mapped payload, no preamble).
// Real radios concatenate: preamble_burst ++ tx_data_symbols.
std::vector<cfloat> phy_tx_data(const std::vector<uint8_t>& bits,
                                const FrameTxConfig& cfg);

struct FrameRxResult {
    bool synced = false;
    bool pci_confirmed = false;
    int pci = -1;
    int timing = -999;       // PSS body start sample index
    float cfo_rad = 0.f;     // estimated radians/sample (before correction)
    float snr_db = -100.f;   // DMRS-based post-equalisation estimate
    std::vector<uint8_t> bits;
};

std::vector<uint8_t> phy_rx_frame(const std::vector<cfloat>& samples,
                                  const FrameTxConfig& cfg, FrameRxResult& out);

}

#endif
