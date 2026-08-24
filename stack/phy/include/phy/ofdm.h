#ifndef AETHER_PHY_OFDM_H
#define AETHER_PHY_OFDM_H

#include "phy/phy_common.h"
#include <complex>
#include <cstdint>
#include <vector>

namespace phy {

// OFDM transmit: frequency-domain symbols -> time-domain samples with CP
// Each OFDM symbol: [CP | IFFT output], where CP = last cp_len samples of IFFT output
std::vector<std::complex<float>> ofdm_tx(
    const std::vector<std::complex<float>>& symbols,
    int n_fft = DEFAULT_N_FFT,
    int cp_len = DEFAULT_CP_LEN);

// OFDM receive: time-domain samples -> frequency-domain symbols
// Strips CP, applies FFT per OFDM symbol
std::vector<std::complex<float>> ofdm_rx(
    const std::vector<std::complex<float>>& samples,
    int n_fft = DEFAULT_N_FFT,
    int cp_len = DEFAULT_CP_LEN);

// Full PHY Tx chain: bits -> QPSK -> OFDM -> time-domain samples
std::vector<std::complex<float>> phy_tx(
    const std::vector<uint8_t>& bits,
    int n_fft = DEFAULT_N_FFT,
    int cp_len = DEFAULT_CP_LEN);

// Full PHY Rx chain: time-domain samples -> OFDM demod -> QPSK demod -> bits
std::vector<uint8_t> phy_rx(
    const std::vector<std::complex<float>>& samples,
    size_t n_data_bits,
    int n_fft = DEFAULT_N_FFT,
    int cp_len = DEFAULT_CP_LEN);

// Full PHY Rx chain with automatic bit-count detection from the sample count.
// Decodes every complete OFDM symbol available (2 bits per subcarrier).
std::vector<uint8_t> phy_rx_auto(
    const std::vector<std::complex<float>>& samples,
    int n_fft = DEFAULT_N_FFT,
    int cp_len = DEFAULT_CP_LEN);

}

#endif
