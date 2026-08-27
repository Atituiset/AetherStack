#ifndef AETHER_PHY_SYNC_H
#define AETHER_PHY_SYNC_H

// M10: physical-layer cell search and synchronisation.
//
// Frame preamble layout (one burst):
//   [CP][PSS symbol][CP][SSS symbol][CP][DMRS symbol][CP][data symbols...]
//
// * PSS  : Zadoff-Chu root sequence, one of three roots indexed by NID2.
//          Sliding correlation against the local roots yields the burst
//          timing offset and narrows the cell identity to three options.
// * SSS  : second ZC root indexed by NID1 -> completes the PCI.
// * DMRS : known QPSK reference symbol derived from the full PCI; used for
//          least-squares channel estimation and for confirming the PCI.

#include <complex>
#include <cstdint>
#include <vector>

namespace phy {

using cfloat = std::complex<float>;

constexpr int kNumNid2 = 3;             // PSS roots
constexpr int kNumNid1 = 2;             // SSS roots per NID2 (teaching scale)
inline int make_pci(int nid1, int nid2) { return nid1 * kNumNid2 + nid2; }
inline int pci_nid1(int pci) { return pci / kNumNid2; }
inline int pci_nid2(int pci) { return pci % kNumNid2; }

// Length-63 base Zadoff-Chu with root u, first `n` entries (i = 0..n-1).
std::vector<cfloat> zadoff_chu(int u, int n);

// Single radix-2 FFT implementation shared by the whole PHY.
// inverse=true -> normalising inverse transform; false -> forward.
void fft_inplace(std::vector<cfloat>& v, bool inverse);

// Frequency-domain PSS/SSS rows for a 64-subcarrier grid (DC nulled).
std::vector<cfloat> pss_freq(int nid2, int n_fft);
std::vector<cfloat> sss_freq(int nid1, int n_fft);

// Time-domain PSS symbol (IFFT of pss_freq) without CP.
std::vector<cfloat> pss_time(int nid2, int n_fft);

struct SyncResult {
    bool found = false;
    int timing = 0;      // sample index of the PSS symbol start (post-CP)
    int nid2_hint = 0;   // NID2 from the PSS root match
    int pci = -1;        // provisional: make_pci(0, nid2_hint)
    float peak = 0.f;    // correlation peak magnitude (normalised)
};

// Slide-correlate `samples` against all PSS roots. Returns the best timing
// offset and NID2 (PCI completed by the caller via SSS/DMRS verification).
// scan_limit caps the search window (samples from the buffer start); the
// default scans everything. Bounding it matters: the correlation is
// O(window × n_fft × roots) per received burst, which dominates the
// receiver CPU on large media frames.
SyncResult pss_detect(const std::vector<cfloat>& samples, int n_fft, int cp_len,
                      size_t scan_limit = static_cast<size_t>(-1));

// Estimate the carrier-frequency offset from the cyclic prefix of one
// symbol located at `sym_start` (post-CP sample index). Returns radians per
// sample.
float cfo_estimate_cp(const std::vector<cfloat>& samples, int sym_start,
                      int n_fft, int cp_len);

void apply_cfo(std::vector<cfloat>& samples, float rad_per_sample);

// Confirm a full PCI by correlating the received SSS/DMRS frequency rows
// against locally generated references. Returns matched filter power [0..1].
float pci_verify(const std::vector<cfloat>& sss_fd,
                 const std::vector<cfloat>& dmrs_fd, int pci, int n_fft);

// Deterministic QPSK DMRS row derived from the PCI (known to both ends).
std::vector<cfloat> dmrs_freq(int pci, int n_fft);

}

#endif
