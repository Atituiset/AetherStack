#include "phy/ofdm.h"
#include "phy/qpsk.h"
#include <cmath>
#include <cstring>

namespace phy {

namespace {

void fft_cooley_tukey(std::complex<float>* data, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * static_cast<float>(M_PI) / len * (inverse ? -1 : 1);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (int j = 0; j < len / 2; ++j) {
                auto u = data[i + j];
                auto v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; ++i) {
            data[i] /= n;
        }
    }
}

}

std::vector<std::complex<float>> ofdm_tx(
    const std::vector<std::complex<float>>& symbols,
    int n_fft,
    int cp_len) {
    size_t n_ofdm_syms = (symbols.size() + n_fft - 1) / n_fft;
    size_t padded_len = n_ofdm_syms * n_fft;
    std::vector<std::complex<float>> padded(padded_len, {0, 0});
    std::memcpy(padded.data(), symbols.data(),
                symbols.size() * sizeof(std::complex<float>));

    size_t symbol_len = static_cast<size_t>(n_fft + cp_len);
    std::vector<std::complex<float>> output;
    output.reserve(n_ofdm_syms * symbol_len);

    for (size_t s = 0; s < n_ofdm_syms; ++s) {
        std::vector<std::complex<float>> chunk(
            padded.begin() + s * n_fft,
            padded.begin() + (s + 1) * n_fft);
        fft_cooley_tukey(chunk.data(), n_fft, true);
        for (int c = 0; c < cp_len; ++c) {
            output.push_back(chunk[n_fft - cp_len + c]);
        }
        output.insert(output.end(), chunk.begin(), chunk.end());
    }
    return output;
}

std::vector<std::complex<float>> ofdm_rx(
    const std::vector<std::complex<float>>& samples,
    int n_fft,
    int cp_len) {
    size_t symbol_len = static_cast<size_t>(n_fft + cp_len);
    size_t n_ofdm_syms = samples.size() / symbol_len;
    if (n_ofdm_syms == 0) return {};

    std::vector<std::complex<float>> output;
    output.reserve(n_ofdm_syms * n_fft);

    for (size_t s = 0; s < n_ofdm_syms; ++s) {
        std::vector<std::complex<float>> chunk(
            samples.begin() + s * symbol_len + cp_len,
            samples.begin() + s * symbol_len + cp_len + n_fft);
        fft_cooley_tukey(chunk.data(), n_fft, false);
        output.insert(output.end(), chunk.begin(), chunk.end());
    }
    return output;
}

std::vector<std::complex<float>> phy_tx(
    const std::vector<uint8_t>& bits,
    int n_fft,
    int cp_len) {
    auto symbols = qpsk_modulate(bits);
    return ofdm_tx(symbols, n_fft, cp_len);
}

std::vector<uint8_t> phy_rx(
    const std::vector<std::complex<float>>& samples,
    size_t n_data_bits,
    int n_fft,
    int cp_len) {
    auto freq = ofdm_rx(samples, n_fft, cp_len);
    size_t n_data_symbols = n_data_bits / 2;
    if (freq.size() < n_data_symbols) return {};
    std::vector<std::complex<float>> data_symbols(
        freq.begin(), freq.begin() + n_data_symbols);
    return qpsk_demodulate(data_symbols);
}

}
