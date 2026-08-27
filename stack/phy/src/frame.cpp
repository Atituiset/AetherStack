#include "phy/frame.h"
#include "phy/qam.h"
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

// Insert the MCS tag byte between the pack_air_bits length prefix and the
// payload bits. Symbol 0 (QPSK) always carries the start of this stream.
std::vector<uint8_t> tag_mcs(const std::vector<uint8_t>& bits, Mcs mcs) {
    std::vector<uint8_t> out;
    out.reserve(bits.size() + 8);
    const size_t head = std::min<size_t>(16, bits.size());
    out.insert(out.end(), bits.begin(), bits.begin() + head);
    const uint8_t v = static_cast<uint8_t>(mcs);
    for (int i = 0; i < 8; ++i) out.push_back((v >> i) & 1); // LSB-first
    out.insert(out.end(), bits.begin() + head, bits.end());
    return out;
}

} // namespace

size_t frame_bits_capacity(int n_fft, Mcs mcs) {
    const int bps = mcs_bits_per_symbol(mcs);
    return static_cast<size_t>(2 * n_fft +
                               (kFrameDataMaxSymbols - 1) * bps * n_fft - 8);
}

int frame_data_symbols(size_t n_bits, int n_fft, Mcs mcs) {
    const int bps = mcs_bits_per_symbol(mcs);
    const size_t total = n_bits + 8; // + MCS tag
    if (total <= static_cast<size_t>(2 * n_fft)) return 1;
    return 1 + static_cast<int>((total - 2 * n_fft + bps * n_fft - 1) /
                                (bps * n_fft));
}

// Map the tagged bit stream onto data symbols: symbol 0 always QPSK, the
// rest at the burst MCS. Returned as per-symbol frequency rows.
std::vector<std::vector<cfloat>> map_data_symbols(
    const std::vector<uint8_t>& bits, const FrameTxConfig& cfg, Mcs mcs) {
    const int bps = mcs_bits_per_symbol(mcs);
    const int kDataSyms = frame_data_symbols(bits.size(), cfg.n_fft, mcs);
    auto tagged = tag_mcs(bits, mcs);

    std::vector<std::vector<cfloat>> rows;
    rows.reserve(kDataSyms);
    // Symbol 0: QPSK.
    {
        std::vector<uint8_t> head(tagged.begin(),
                                  tagged.begin() +
                                      std::min(tagged.size(),
                                               static_cast<size_t>(2 * cfg.n_fft)));
        head.resize(2 * cfg.n_fft, 0);
        rows.push_back(qam_modulate(head, 2));
    }
    // Symbols 1..N-1: burst MCS.
    for (int s = 1; s < kDataSyms; ++s) {
        const size_t off = 2 * cfg.n_fft +
                           static_cast<size_t>(s - 1) * bps * cfg.n_fft;
        std::vector<uint8_t> chunk(
            tagged.begin() + std::min(off, tagged.size()),
            tagged.begin() + std::min(off + bps * cfg.n_fft, tagged.size()));
        chunk.resize(bps * cfg.n_fft, 0);
        rows.push_back(qam_modulate(chunk, bps));
    }
    return rows;
}

std::vector<cfloat> phy_tx_frame(const std::vector<uint8_t>& bits,
                                 const FrameTxConfig& cfg, Mcs mcs) {
    if (bits.size() > frame_bits_capacity(cfg.n_fft, mcs)) {
        throw std::invalid_argument("phy_tx_frame: payload exceeds capacity");
    }
    auto rows = map_data_symbols(bits, cfg, mcs);

    std::vector<std::vector<cfloat>> sym_time;
    sym_time.reserve(kPreambleSymbols + rows.size());
    auto to_time = [&](const std::vector<cfloat>& fd) {
        auto t = fd;
        fft_inplace(t, true);
        return t;
    };
    sym_time.push_back(to_time(pss_freq(pci_nid2(cfg.pci), cfg.n_fft)));
    sym_time.push_back(to_time(sss_freq(pci_nid1(cfg.pci), cfg.n_fft)));
    sym_time.push_back(to_time(dmrs_freq(cfg.pci, cfg.n_fft)));
    for (auto& row : rows) {
        fft_inplace(row, true);
        sym_time.push_back(std::move(row));
    }

    std::vector<cfloat> out;
    out.reserve(sym_time.size() *
                static_cast<size_t>(cfg.n_fft + cfg.cp_len));
    for (const auto& t : sym_time) add_symbol(out, t, cfg.cp_len);
    return out;
}

std::vector<cfloat> phy_tx_data(const std::vector<uint8_t>& bits,
                                const FrameTxConfig& cfg, Mcs mcs) {
    if (bits.size() > frame_bits_capacity(cfg.n_fft, mcs)) {
        throw std::invalid_argument("phy_tx_data: payload exceeds capacity");
    }
    auto rows = map_data_symbols(bits, cfg, mcs);
    std::vector<cfloat> out;
    out.reserve(rows.size() * static_cast<size_t>(cfg.n_fft + cfg.cp_len));
    for (auto& row : rows) {
        fft_inplace(row, true);
        add_symbol(out, row, cfg.cp_len);
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
                                  FrameRxResult& out,
                                  uint16_t own_rnti) {
    out = {};

    // --- 1. timing + NID2 via PSS correlation --------------------------
    // Every received datagram holds exactly one burst whose preamble starts
    // at (or within a few samples of) the buffer start, so a full-buffer
    // sliding correlation is wasted work — it scales with the DATA length
    // and was the dominant receiver CPU cost on large media frames. Scan
    // only a small sync window (a few symbol strides covers any realistic
    // timing offset; the sync tests offset by 37 samples).
    const size_t scan_window = static_cast<size_t>(4 * (cfg.n_fft + cfg.cp_len));
    auto sync = pss_detect(samples, cfg.n_fft, cfg.cp_len, scan_window);
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
    // M22: cfg.pci < 0 is the wildcard "any cell" used by UEs in
    // multi-cell deployments — they must hear every cell's bursts (cell
    // demux happens at the SIB1 cell_id / RNTI layers above).
    if (best_pci < 0 || best_corr < 0.5f ||
        (cfg.pci >= 0 && best_pci != cfg.pci)) {
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
    double ref_pow = 0, rx_pow = 0;
    for (int k = 0; k < cfg.n_fft; ++k) {
        if (std::norm(ref_dmrs[k]) > 0) {
            H[k] = rx_dmrs[k] / ref_dmrs[k];
        }
        ref_pow += std::norm(ref_dmrs[k]);
        rx_pow += std::norm(rx_dmrs[k]);
    }
    // M19: SNR proxy must be GAIN-INVARIANT — UL bursts arrive scaled by
    // the UE's TX power and comparing against the unit-scale reference
    // directly read the gain mismatch as noise, which made the TPC loop
    // unstable (power-up lowered the "SNR" -> more power-up). Estimate the
    // scalar gain first, then measure the deviation from the SCALED
    // reference: sig = |g_hat*ref|^2, noise = |rx - g_hat*ref|^2.
    const double g_hat = rx_pow > 0 && ref_pow > 0
                             ? std::sqrt(rx_pow / ref_pow)
                             : 1.0;
    double noise_pow = 0;
    for (int k = 1; k < cfg.n_fft; ++k) {
        cfloat err = rx_dmrs[k] - static_cast<float>(g_hat) * ref_dmrs[k];
        noise_pow += std::norm(err);
    }
    const double sig_pow = g_hat * g_hat * ref_pow;
    out.snr_db = noise_pow > 0
                     ? 10.0f * std::log10(static_cast<float>(sig_pow /
                                                            noise_pow))
                     : 60.0f;

    // --- 6. Equalise data symbols and demodulate -----------------------
    // Data symbol s occupies [preamble_total + s*stride + cp, + n_fft).
    // Symbol 0 is always QPSK and holds [len:16][mcs:8][...]; the burst MCS
    // read from it selects the demodulator for the remaining symbols.
    const int preamble_total = kPreambleSymbols * stride;
    int avail_syms = static_cast<int>(work.size()) >=
                             preamble_total + cfg.cp_len + cfg.n_fft
                         ? 1 + (work.size() - preamble_total - cfg.cp_len -
                                cfg.n_fft) /
                                   stride
                         : 0;
    avail_syms = std::min(avail_syms, kFrameDataMaxSymbols);
    if (avail_syms <= 0) return {};

    auto equalise = [&](int sym_index) {
        auto fd = slice_fd(sym_index);
        std::vector<cfloat> eq(fd.size());
        for (int k = 0; k < cfg.n_fft; ++k) {
            eq[k] = (std::norm(H[k]) > 1e-6f)
                        ? fd[k] / H[k]
                        : cfloat(0, 0);
        }
        return eq;
    };

    // Symbol 0: QPSK header + first payload bits.
    auto head_bits = qam_demodulate(equalise(kPreambleSymbols), 2);
    if (head_bits.size() < 48) return {};
    auto rd = [](const std::vector<uint8_t>& b, size_t off, int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            v |= static_cast<uint32_t>(b[off + i] & 1) << i;
        }
        return v;
    };
    const uint32_t mcs_field = rd(head_bits, 16, 8);
    if (mcs_field > static_cast<uint32_t>(Mcs::QAM64)) {
        return {}; // corrupt header (or a peer we don't understand)
    }
    const Mcs mcs = static_cast<Mcs>(mcs_field);
    out.mcs = mcs;
    const int bps = mcs_bits_per_symbol(mcs);

    std::vector<uint8_t> tagged = std::move(head_bits); // [len][mcs][payload]

    // Early foreign-unicast drop (own_rnti filter): symbol 0 holds
    // [len:16][mcs:8][type:8][rnti:16] (LSB-first, 48 bits).
    if (own_rnti != 0) {
        const uint32_t type = rd(tagged, 24, 8);
        const uint32_t rnti = rd(tagged, 32, 16);
        constexpr uint32_t kDataType = 0xA5;      // AirFrameType::DATA
        constexpr uint32_t kBroadcast = 0xFFFF;   // mac::RNTI_BROADCAST
        if (type == kDataType && rnti != own_rnti && rnti != kBroadcast) {
            return {}; // not for us: skip demodulating the rest
        }
    }

    // Symbols 1..N-1 at the burst MCS.
    for (int s = 1; s < avail_syms; ++s) {
        auto sbits = qam_demodulate(equalise(kPreambleSymbols + s), bps);
        tagged.insert(tagged.end(), sbits.begin(), sbits.end());
    }

    // Strip the 8-bit MCS tag: [len:16][payload...] (pack_air_bits format).
    std::vector<uint8_t> bits;
    bits.reserve(tagged.size() - 8);
    bits.insert(bits.end(), tagged.begin(), tagged.begin() + 16);
    bits.insert(bits.end(), tagged.begin() + 24, tagged.end());
    return bits;
}

}
