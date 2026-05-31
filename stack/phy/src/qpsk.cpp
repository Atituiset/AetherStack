#include "phy/qpsk.h"
#include "phy/phy_common.h"
#include <cmath>

namespace phy {

std::vector<std::complex<float>> qpsk_modulate(const std::vector<uint8_t>& bits) {
    if (bits.size() % 2 != 0) {
        return {};
    }
    size_t n_symbols = bits.size() / 2;
    std::vector<std::complex<float>> symbols;
    symbols.reserve(n_symbols);
    for (size_t i = 0; i < n_symbols; ++i) {
        float i_val = (bits[2 * i] == 0) ? 1.0f : -1.0f;
        float q_val = (bits[2 * i + 1] == 0) ? 1.0f : -1.0f;
        symbols.emplace_back(i_val * static_cast<float>(QPSK_NORM),
                             q_val * static_cast<float>(QPSK_NORM));
    }
    return symbols;
}

std::vector<uint8_t> qpsk_demodulate(const std::vector<std::complex<float>>& symbols) {
    std::vector<uint8_t> bits;
    bits.reserve(symbols.size() * 2);
    float sqrt2 = static_cast<float>(QPSK_NORM);
    for (const auto& s : symbols) {
        float i_val = s.real() / sqrt2;
        float q_val = s.imag() / sqrt2;
        bits.push_back(i_val < 0 ? 1 : 0);
        bits.push_back(q_val < 0 ? 1 : 0);
    }
    return bits;
}

}
