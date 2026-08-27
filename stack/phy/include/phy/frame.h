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
#include "phy/qam.h"
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
// kFrameDataMaxSymbols caps a single burst. 40 symbols @64 FFT QPSK = 640 B
// of air-frame payload, enough for one RLC AM segment (256 B) plus all
// headers — M16 voice/video media SDUs need this headroom.
constexpr int kFrameDataMaxSymbols = 40;

// M19: every burst is MCS-tagged. Data symbol 0 is ALWAYS QPSK and carries
// the bit stream [len:16][mcs:8][payload...] (the MCS byte is inserted by
// the PHY after the pack_air_bits length prefix and stripped again on
// receive — upper layers never see it); symbols 1..N use the burst MCS.
// The fixed-QPSK header keeps foreign-unicast early-drop and mixed-rate
// reception working; a legacy peer would misread a high-MCS burst (single
// software version is assumed — see docs/m19_plan.md).
int frame_data_symbols(size_t n_bits, int n_fft,
                       Mcs mcs = Mcs::QPSK);   // symbols needed (input bits)
size_t frame_bits_capacity(int n_fft,
                           Mcs mcs = Mcs::QPSK); // max input bits

std::vector<cfloat> phy_tx_frame(const std::vector<uint8_t>& bits,
                                 const FrameTxConfig& cfg,
                                 Mcs mcs = Mcs::QPSK);

// Preamble-only burst (PSS+SSS+DMRS with CPs). Real radios prepend this to
// the modulated data symbols of every transmission so a receiver can sync,
// identify the cell and estimate the channel before touching the data.
std::vector<cfloat> phy_preamble_burst(const FrameTxConfig& cfg);

// Data symbols only (IFFT of the mapped payload, no preamble).
// Real radios concatenate: preamble_burst ++ tx_data_symbols.
std::vector<cfloat> phy_tx_data(const std::vector<uint8_t>& bits,
                                const FrameTxConfig& cfg,
                                Mcs mcs = Mcs::QPSK);

struct FrameRxResult {
    bool synced = false;
    bool pci_confirmed = false;
    int pci = -1;
    int timing = -999;       // PSS body start sample index
    float cfo_rad = 0.f;     // estimated radians/sample (before correction)
    float snr_db = -100.f;   // DMRS-based post-equalisation estimate
    Mcs mcs = Mcs::QPSK;     // burst MCS read from the header symbol (M19)
    std::vector<uint8_t> bits;
};

// Decode one burst. When `own_rnti` is non-zero, unicast DATA bursts
// addressed to a DIFFERENT rnti are dropped right after the first data
// symbol (the length/type/rnti fields fit in it) — the receiver skips
// demodulating the rest of the frame. On a shared medium with downlink
// fan-out this halves the PHY cost of foreign traffic, mirroring how a
// real UE skips PDSCH payloads the PDCCH did not grant to it.
std::vector<uint8_t> phy_rx_frame(const std::vector<cfloat>& samples,
                                  const FrameTxConfig& cfg, FrameRxResult& out,
                                  uint16_t own_rnti = 0);

}

#endif
