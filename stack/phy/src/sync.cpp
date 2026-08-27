#include "phy/sync.h"
#include <cmath>
#include <stdexcept>

namespace phy {

// Single shared radix-2 FFT (M10): one implementation for sync + framing so
// correctness is testable in one place.
void fft_inplace(std::vector<cfloat>& v, bool inverse) {
    const int n = static_cast<int>(v.size());
    if (n <= 1) return;
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(v[i], v[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        // Standard convention: forward uses exp(-j2pi kn/N), inverse uses
        // exp(+j2pi kn/N) with a 1/N scale.
        float ang = 2.0f * static_cast<float>(M_PI) / len *
                    (inverse ? 1.0f : -1.0f);
        cfloat wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cfloat w(1);
            for (int j = 0; j < len / 2; ++j) {
                cfloat u = v[i + j], t = v[i + j + len / 2] * w;
                v[i + j] = u + t;
                v[i + j + len / 2] = u - t;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (auto& x : v) x /= static_cast<float>(n);
    }
}

std::vector<cfloat> zadoff_chu(int u, int n) {
    if (n % 2 == 0) throw std::invalid_argument("ZC requires odd length");
    std::vector<cfloat> z(n);
    float cf = static_cast<float>(u);
    float cn = static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        float phase = -static_cast<float>(M_PI) * cf * i * (i + 1) / cn;
        z[i] = {std::cos(phase), std::sin(phase)};
    }
    return z;
}

namespace {

// Shared subcarrier mapping: DC null; ZC[0..n_fft-2] onto k=1..n_fft-1.
std::vector<cfloat> zc_row(const std::vector<cfloat>& zc, int n_fft) {
    std::vector<cfloat> row(n_fft, {0, 0});
    for (int k = 1; k < n_fft && k - 1 < static_cast<int>(zc.size()); ++k) {
        row[k] = zc[k - 1];
    }
    return row;
}

} // namespace

std::vector<cfloat> pss_freq(int nid2, int n_fft) {
    // LTE PSS roots, reduced modulo the table to our three-cell identity.
    static constexpr int kRoots[kNumNid2] = {25, 29, 34};
    int root = kRoots[nid2 % kNumNid2];
    return zc_row(zadoff_chu(root, n_fft - 1), n_fft);
}

std::vector<cfloat> sss_freq(int nid1, int n_fft) {
    // Teaching-grade SSS: second ZC family keyed by NID1.
    static constexpr int kRoots[kNumNid1] = {7, 13};
    int root = kRoots[nid1 % kNumNid1];
    return zc_row(zadoff_chu(root, n_fft - 1), n_fft);
}

std::vector<cfloat> dmrs_freq(int pci, int n_fft) {
    // Deterministic QPSK reference derived from the PCI via an LCG PRBS.
    uint32_t lcg = 0x9E3779B9u ^ (static_cast<uint32_t>(pci) * 2654435761u);
    auto next_bit = [&lcg]() {
        lcg = lcg * 1664525u + 1013904223u;
        return (lcg >> 16) & 1;
    };
    std::vector<cfloat> row(n_fft, {0, 0});
    float norm = 1.0f / std::sqrt(2.0f);
    // Full-grid reference including DC so the LS estimate covers every
    // data subcarrier (this link has no analog DC impairment).
    for (int k = 0; k < n_fft; ++k) {
        float i_val = (next_bit() ? -1.0f : 1.0f) * norm;
        float q_val = (next_bit() ? -1.0f : 1.0f) * norm;
        row[k] = {i_val, q_val};
    }
    return row;
}

std::vector<cfloat> pss_time(int nid2, int n_fft) {
    auto fd = pss_freq(nid2, n_fft);
    fft_inplace(fd, true);
    return fd;
}

SyncResult pss_detect(const std::vector<cfloat>& samples, int n_fft,
                      int cp_len, size_t scan_limit) {
    (void)cp_len;
    SyncResult out;
    if (static_cast<int>(samples.size()) < n_fft) return out;

    // PSS time templates are per (nid2, n_fft); regenerating the Zadoff-Chu
    // + IFFT for every received burst was a measurable CPU sink.
    static std::vector<cfloat> tpl_cache[kNumNid2];
    static int tpl_n_fft = -1;
    if (tpl_n_fft != n_fft) {
        for (auto& t : tpl_cache) t.clear();
        tpl_n_fft = n_fft;
    }

    float best_peak = -1.f;
    int best_timing = -1, best_nid2 = 0;
    const size_t last_start =
        std::min(samples.size() - static_cast<size_t>(n_fft), scan_limit);

    for (int nid2 = 0; nid2 < kNumNid2; ++nid2) {
        if (tpl_cache[nid2].empty()) tpl_cache[nid2] = pss_time(nid2, n_fft);
        const auto& tpl = tpl_cache[nid2];
        double energy = 0;
        for (const auto& x : tpl) energy += std::norm(x);
        if (energy <= 0) continue;

        for (size_t start = 0; start <= last_start; ++start) {
            cfloat corr(0, 0);
            double win_energy = 0;
            for (int k = 0; k < n_fft; ++k) {
                corr += samples[start + k] * std::conj(tpl[k]);
                win_energy += std::norm(samples[start + k]);
            }
            if (win_energy <= 0 || energy <= 0) continue;
            // Matched-filter normalisation: 1.0 at a perfect match.
            float mag = static_cast<float>(
                std::norm(corr) / (energy * win_energy));
            if (mag > best_peak) {
                best_peak = mag;
                best_timing = static_cast<int>(start);
                best_nid2 = nid2;
            }
        }
    }

    if (best_timing >= 0 && best_peak > 0.5f) {
        out.found = true;
        out.timing = best_timing;
        out.nid2_hint = best_nid2;
        out.pci = make_pci(0, best_nid2); // provisional; caller confirms
        out.peak = best_peak;
    }
    return out;
}

float cfo_estimate_cp(const std::vector<cfloat>& samples, int sym_start,
                      int n_fft, int cp_len) {
    // Classic CP correlation: r[n]·conj(r[n+N]) over the CP window yields
    // exp(j*2pi*f*N/fs); arg() divided by N gives radians per sample.
    cfloat sum(0, 0);
    for (int i = 0; i < cp_len; ++i) {
        sum += samples[sym_start - cp_len + i] *
               std::conj(samples[sym_start + i]);
    }
    return std::arg(sum) / static_cast<float>(n_fft);
}

void apply_cfo(std::vector<cfloat>& samples, float rad_per_sample) {
    float phase = 0.f;
    for (auto& x : samples) {
        cfloat rot(std::cos(phase), -std::sin(phase));
        x *= rot;
        phase += rad_per_sample;
    }
}

namespace {

// Normalised correlation of a received frequency row against a reference:
// 1.0 at a perfect match, independent of the row's absolute power.
float row_corr(const std::vector<cfloat>& rx, const std::vector<cfloat>& ref,
               int n_fft) {
    double num = 0, er = 0, ef = 0;
    for (int k = 1; k < n_fft; ++k) {
        num += std::real(rx[k] * std::conj(ref[k]));
        er += std::norm(rx[k]);
        ef += std::norm(ref[k]);
    }
    if (er <= 0 || ef <= 0) return 0.f;
    return static_cast<float>(num / std::sqrt(er * ef));
}

} // namespace

float pci_verify(const std::vector<cfloat>& sss_fd,
                 const std::vector<cfloat>& dmrs_fd, int pci, int n_fft) {
    float c_sss = row_corr(sss_fd, sss_freq(pci_nid1(pci), n_fft), n_fft);
    float c_dmrs = row_corr(dmrs_fd, dmrs_freq(pci, n_fft), n_fft);
    return 0.5f * (c_sss + c_dmrs);
}

}
