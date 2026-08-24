#include "phy/frame.h"
#include "phy/qpsk.h"
#include "phy/sync.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace phy {

namespace {

constexpr int kPreambleSymbols = 3; // PSS, SSS, DMRS

void add_symbol(std::vector<cfloat>& out, const std::vector<cfloat>& time_sym,
                int cp_len) {
    size_t base = out.size();
    out.resize(base + time_sym.size() + cp_len);
    // CP = tail of the symbol
    std::memcpy(out.data() + base,
                time_sym.data() + time_sym.size() - cp_len,
                cp_len * sizeof(cfloat));
    std::memcpy(out.data() + base + cp_len, time_sym.data(),
                time_sym.size() * sizeof(cfloat));
}

} // namespace

size_t frame_bits_capacity(int n_fft) {
    return static_cast<size_t>(kFrameDataMaxSymbols) * n_fft * 2; // QPSK
}

int frame_data_symbols(size_t n_bits, int n_fft) {
    size_t qpsk_syms = (n_bits + 1) / 2;
    int syms = static_cast<int>((qpsk_syms + n_fft - 1) / n_fft);
    return std::max(syms, 1);
}

std::vector<cfloat> phy_tx_frame(const std::vector<uint8_t>& bits,
                                 const FrameTxConfig& cfg) {
    if (static_cast<int>(bits.size()) > frame_bits_capacity(cfg.n_fft)) {
        throw std::invalid_argument("phy_tx_frame: payload exceeds capacity");
    }
    const int kDataSyms = frame_data_symbols(bits.size(), cfg.n_fft);

    auto symbols = qpsk_modulate(bits);
    symbols.resize(static_cast<size_t>(kDataSyms) * cfg.n_fft, {0, 0});

    std::vector<std::vector<cfloat>> sym_time;
    sym_time.reserve(kPreambleSymbols + kFrameDataMaxSymbols);

    auto to_time = [&](const std::vector<cfloat>& fd) {
        auto t = fd;
        fft_inplace(t, true);
        return t;
    };
    sym_time.push_back(to_time(pss_freq(pci_nid2(cfg.pci), cfg.n_fft)));
    sym_time.push_back(to_time(sss_freq(pci_nid1(cfg.pci), cfg.n_fft)));
    sym_time.push_back(to_time(dmrs_freq(cfg.pci, cfg.n_fft)));

    for (int s = 0; s < kDataSyms; ++s) {
        std::vector<cfloat> chunk(symbols.begin() + s * cfg.n_fft,
                                  symbols.begin() + (s + 1) * cfg.n_fft);
        // The data subcarriers need the same frequency->time transform as
        // the preamble symbols.
        fft_inplace(chunk, true);
        sym_time.push_back(std::move(chunk));
    }

    std::vector<cfloat> out;
    out.reserve(sym_time.size() *
                static_cast<size_t>(cfg.n_fft + cfg.cp_len));
    for (const auto& t : sym_time) add_symbol(out, t, cfg.cp_len);
    return out;
}

std::vector<cfloat> phy_tx_data(const std::vector<uint8_t>& bits,
                                const FrameTxConfig& cfg) {
    const int kDataSyms = frame_data_symbols(bits.size(), cfg.n_fft);
    auto symbols = qpsk_modulate(bits);
    symbols.resize(static_cast<size_t>(kDataSyms) * cfg.n_fft, {0, 0});

    std::vector<cfloat> out;
    out.reserve(static_cast<size_t>(kDataSyms) * (cfg.n_fft + cfg.cp_len));
    for (int s = 0; s < kDataSyms; ++s) {
        std::vector<cfloat> chunk(symbols.begin() + s * cfg.n_fft,
                                  symbols.begin() + (s + 1) * cfg.n_fft);
        fft_inplace(chunk, true);
        add_symbol(out, chunk, cfg.cp_len);
    }
    return out;
}

std::vector<cfloat> phy_preamble_burst(const FrameTxConfig& cfg) {
    std::vector<std::vector<cfloat>> sym_time;
    auto to_time = [&](const std::vector<cfloat>& fd) {
        auto t = fd;
        fft_inplace(t, true);
        return t;
    };
    sym_time.push_back(to_time(pss_freq(pci_nid2(cfg.pci), cfg.n_fft)));
    sym_time.push_back(to_time(sss_freq(pci_nid1(cfg.pci), cfg.n_fft)));
    sym_time.push_back(to_time(dmrs_freq(cfg.pci, cfg.n_fft)));

    std::vector<cfloat> out;
    out.reserve(sym_time.size() *
                static_cast<size_t>(cfg.n_fft + cfg.cp_len));
    for (const auto& t : sym_time) add_symbol(out, t, cfg.cp_len);
    return out;
}

std::vector<uint8_t> phy_rx_frame(const std::vector<cfloat>& samples,
                                  const FrameTxConfig& cfg,
                                  FrameRxResult& out) {
    out = {};

    // --- 1. timing + NID2 via PSS correlation --------------------------
    auto sync = pss_detect(samples, cfg.n_fft, cfg.cp_len);
    if (!sync.found) return {};
    int pss_start = sync.timing;
    out.timing = sync.timing;

    // Symbol stride: [CP][N] per OFDM symbol.
    const int stride = cfg.n_fft + cfg.cp_len;
    // The PSS correlation peak lands on the post-CP body of the PSS symbol.
    // Rewind to the CP start of that symbol, then step forward whole
    // symbols: SSS at +stride, DMRS at +2*stride, data from +3*stride.
    int burst_cp_start = pss_start - cfg.cp_len;

    // --- 2. CFO ----------------------------------------------------------
    // Not compensated in M10: the current channel model injects no carrier
    // frequency offset. cfo_estimate_cp() stays available for the future,
    // but note it needs a known-structure reference (not random QPSK) to
    // give a clean estimate.
    float cfo = 0.f;
    out.cfo_rad = cfo;

    // Working window starts at the PSS cyclic prefix.
    int avail = static_cast<int>(samples.size()) - burst_cp_start;
    if (avail < stride * (kPreambleSymbols + kFrameDataMaxSymbols)) {
        // Too short for the full burst; still attempt with what we have.
        if (avail <= 0) return {};
    }
    std::vector<cfloat> work(samples.begin() + burst_cp_start,
                             samples.begin() + burst_cp_start +
                                 std::min(avail, stride *
                                                     (kPreambleSymbols +
                                                      kFrameDataMaxSymbols)));
    apply_cfo(work, -cfo);

    // --- 3. FFT each symbol --------------------------------------------
    auto slice_fd = [&](int sym_index) {
        std::vector<cfloat> fd(work.begin() + sym_index * stride + cfg.cp_len,
                               work.begin() + sym_index * stride + cfg.cp_len +
                                   cfg.n_fft);
        fft_inplace(fd, false);
        return fd;
    };

    int sss_fd_at = 1, dmrs_fd_at = 2;

    // --- 4. PCI confirmation over all candidate NID1 -------------------
    auto rx_sss = slice_fd(sss_fd_at);
    auto rx_dmrs = slice_fd(dmrs_fd_at);
    int best_pci = -1;
    float best_corr = -1.f;
    for (int nid1 = 0; nid1 < kNumNid1; ++nid1) {
        int cand = make_pci(nid1, sync.nid2_hint);
        float corr = pci_verify(rx_sss, rx_dmrs, cand, cfg.n_fft);
        if (corr > best_corr) {
            best_corr = corr;
            best_pci = cand;
        }
    }
    if (best_pci < 0 || best_corr < 0.5f || best_pci != cfg.pci) {
        // Not our cell (or too noisy). Caller may retry with the reported
        // pci when it differs from the expected one.
        out.synced = true;
        out.pci = best_pci;
        out.pci_confirmed = false;
        return {};
    }
    out.synced = true;
    out.pci_confirmed = true;
    out.pci = best_pci;

    // --- 5. LS channel estimate from the DMRS --------------------------
    auto ref_dmrs = dmrs_freq(best_pci, cfg.n_fft);
    std::vector<cfloat> H(cfg.n_fft, {0, 0});
    double sig_pow = 0;
    for (int k = 0; k < cfg.n_fft; ++k) {
        if (std::norm(ref_dmrs[k]) > 0) {
            H[k] = rx_dmrs[k] / ref_dmrs[k];
        }
        sig_pow += std::norm(ref_dmrs[k]);
    }
    // SNR proxy: DMRS power vs. deviation from the clean reference.
    double noise_pow = 0;
    for (int k = 1; k < cfg.n_fft; ++k) {
        cfloat err = rx_dmrs[k] - ref_dmrs[k];
        noise_pow += std::norm(err);
    }
    out.snr_db = noise_pow > 0
                     ? 10.0f * std::log10(static_cast<float>(sig_pow /
                                                            noise_pow))
                     : 60.0f;

    // --- 6. Equalise data symbols and demodulate -----------------------
    // Data symbol s occupies [preamble_total + s*stride + cp, + n_fft).
    const int preamble_total = kPreambleSymbols * stride;
    int avail_syms = static_cast<int>(work.size()) >=
                             preamble_total + cfg.cp_len + cfg.n_fft
                         ? 1 + (work.size() - preamble_total - cfg.cp_len -
                                cfg.n_fft) /
                                   stride
                         : 0;
    avail_syms = std::min(avail_syms, kFrameDataMaxSymbols);
    std::vector<uint8_t> bits;
    bits.reserve(frame_bits_capacity(cfg.n_fft));
    for (int s = 0; s < avail_syms; ++s) {
        auto fd = slice_fd(kPreambleSymbols + s);
        std::vector<cfloat> eq(fd.size());
        for (int k = 0; k < cfg.n_fft; ++k) {
            eq[k] = (std::norm(H[k]) > 1e-6f)
                        ? fd[k] / H[k]
                        : cfloat(0, 0);
        }
        auto sbits = qpsk_demodulate(eq);
        bits.insert(bits.end(), sbits.begin(), sbits.end());
    }
    return bits;
}

}
