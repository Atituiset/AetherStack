#ifndef AETHER_PHY_QAM_H
#define AETHER_PHY_QAM_H

// M19: link adaptation — generic Gray-coded square QAM so one burst can be
// modulated at different rates (QPSK/16QAM/64QAM) selected per link.
//
// Bit order: stream order, m bits per dimension MSB-first, Gray-mapped onto
// PAM levels ±1,±3,...,±(L-1) normalised to unit average symbol power
// (matches the legacy QPSK mapping power; the bit->sign convention differs
// but modulate/demodulate are always used as a pair).

#include <complex>
#include <cstdint>
#include <vector>

namespace phy {

using cfloat = std::complex<float>;

// Modulation-and-coding scheme index carried in every burst's header symbol.
enum class Mcs : uint8_t { QPSK = 0, QAM16 = 1, QAM64 = 2 };

int mcs_bits_per_symbol(Mcs mcs);         // 2 | 4 | 6 (0 for unknown)
const char* mcs_name(Mcs mcs);            // "qpsk" | "16qam" | "64qam"

// bits.size() must be a multiple of bps; empty vector on violation.
std::vector<cfloat> qam_modulate(const std::vector<uint8_t>& bits, int bps);
std::vector<uint8_t> qam_demodulate(const std::vector<cfloat>& symbols,
                                    int bps);

}

#endif
