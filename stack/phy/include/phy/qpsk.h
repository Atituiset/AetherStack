#ifndef AETHER_PHY_QPSK_H
#define AETHER_PHY_QPSK_H

#include <complex>
#include <cstdint>
#include <vector>

namespace phy {

// QPSK modulation: 2 bits -> 1 complex symbol (normalized to unit avg power)
// Mapping (Gray coded):
//   00 -> (+1+1j)/sqrt2, 01 -> (-1+1j)/sqrt2
//   10 -> (+1-1j)/sqrt2, 11 -> (-1-1j)/sqrt2
std::vector<std::complex<float>> qpsk_modulate(const std::vector<uint8_t>& bits);

// QPSK hard demodulation: 1 complex symbol -> 2 bits
// Decision: real>0 -> I_bit=0, imag>0 -> Q_bit=0
std::vector<uint8_t> qpsk_demodulate(const std::vector<std::complex<float>>& symbols);

}

#endif
