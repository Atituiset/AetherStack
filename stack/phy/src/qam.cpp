#include "phy/qam.h"
#include <algorithm>
#include <cmath>

namespace phy {

int mcs_bits_per_symbol(Mcs mcs) {
    switch (mcs) {
        case Mcs::QPSK: return 2;
        case Mcs::QAM16: return 4;
        case Mcs::QAM64: return 6;
    }
    return 0;
}

const char* mcs_name(Mcs mcs) {
    switch (mcs) {
        case Mcs::QPSK: return "qpsk";
        case Mcs::QAM16: return "16qam";
        case Mcs::QAM64: return "64qam";
    }
    return "qpsk";
}

namespace {

// Average power of one PAM dimension with L levels spaced by 2: (L^2-1)/3.
// Scale so I+Q have unit combined average power.
float pam_norm(int bps) {
    const int m = bps / 2;
    const int l = 1 << m;
    return std::sqrt(3.0f / (2.0f * (l * l - 1)));
}

uint32_t gray_encode(uint32_t v) { return v ^ (v >> 1); }

uint32_t gray_decode(uint32_t g) {
    uint32_t v = g;
    while (g >>= 1) v ^= g;
    return v;
}

} // namespace

std::vector<cfloat> qam_modulate(const std::vector<uint8_t>& bits, int bps) {
    if (bps <= 0 || (bps & 1) != 0 || bits.size() % bps != 0) return {};
    const int m = bps / 2;
    const float norm = pam_norm(bps);
    const int l = 1 << m;
    std::vector<cfloat> out;
    out.reserve(bits.size() / bps);
    for (size_t i = 0; i < bits.size(); i += bps) {
        uint32_t vi = 0, vq = 0;
        for (int b = 0; b < m; ++b) {
            vi = (vi << 1) | (bits[i + b] & 1);
            vq = (vq << 1) | (bits[i + m + b] & 1);
        }
        const float ai = (2 * static_cast<int>(gray_encode(vi)) - (l - 1)) * norm;
        const float aq = (2 * static_cast<int>(gray_encode(vq)) - (l - 1)) * norm;
        out.emplace_back(ai, aq);
    }
    return out;
}

std::vector<uint8_t> qam_demodulate(const std::vector<cfloat>& symbols,
                                    int bps) {
    if (bps <= 0 || (bps & 1) != 0) return {};
    const int m = bps / 2;
    const float norm = pam_norm(bps);
    const int l = 1 << m;
    auto slice = [&](float a) {
        // Nearest valid PAM level (odd integers in [-(L-1), L-1]), then
        // inverse Gray.
        int lvl = static_cast<int>(std::lround(a / norm));
        lvl = std::max(-(l - 1), std::min(l - 1, lvl));
        if ((lvl & 1) == 0) lvl += (lvl < 0) ? 1 : -1;
        const int g = (lvl + (l - 1)) / 2;
        return static_cast<uint32_t>(g);
    };
    std::vector<uint8_t> bits;
    bits.reserve(symbols.size() * bps);
    for (const auto& s : symbols) {
        const uint32_t vi = gray_decode(slice(s.real()));
        const uint32_t vq = gray_decode(slice(s.imag()));
        for (int b = m - 1; b >= 0; --b) {
            bits.push_back(static_cast<uint8_t>((vi >> b) & 1));
        }
        for (int b = m - 1; b >= 0; --b) {
            bits.push_back(static_cast<uint8_t>((vq >> b) & 1));
        }
    }
    return bits;
}

}
