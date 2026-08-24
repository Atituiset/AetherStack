#include "phy/fec.h"

#include <cstring>
#include <stdexcept>

namespace phy {

namespace {

constexpr int K = 7;                 // constraint length
constexpr int NUM_STATES = 1 << (K - 1); // 64

// Polynomials in octal: G1=133o, G2=171o. Bit i of the generator masks
// tap i (i=0 is the current input bit).
constexpr uint32_t G1 = 0133;
constexpr uint32_t G2 = 0171;

inline int parity(uint32_t v) {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1;
}

struct Trellis {
    // next_state[input] and output bits for every state.
    uint8_t next[NUM_STATES][2];
    uint8_t out[NUM_STATES][2]; // {G1 bit, G2 bit}

    Trellis() {
        for (int s = 0; s < NUM_STATES; ++s) {
            for (int in = 0; in <= 1; ++in) {
                uint32_t reg = (static_cast<uint32_t>(in) << (K - 1)) | s;
                int b1 = parity(reg & G1);
                int b2 = parity(reg & G2);
                int ns = static_cast<int>(reg >> 1);
                next[s][in] = static_cast<uint8_t>(ns);
                out[s][in] = static_cast<uint8_t>((b1 << 1) | b2);
            }
        }
    }
};

const Trellis& trellis() {
    static const Trellis t;
    return t;
}

} // namespace

uint16_t crc16(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::vector<uint8_t> crc_attach(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> block = payload;
    uint16_t fcs = crc16(payload);
    block.push_back(static_cast<uint8_t>(fcs & 0xFF));
    block.push_back(static_cast<uint8_t>((fcs >> 8) & 0xFF));
    return block;
}

bool crc_verify_strip(const std::vector<uint8_t>& block,
                      std::vector<uint8_t>& payload) {
    if (block.size() < 2) return false;
    payload.assign(block.begin(), block.end() - 2);
    return crc16(payload) ==
           static_cast<uint16_t>(block[block.size() - 2] |
                                 (block[block.size() - 1] << 8));
}

size_t coded_size(size_t n_info_bits) {
    // rate 1/2 + (K-1) tail bits, also rate 1/2.
    return (n_info_bits + (K - 1)) * 2;
}

std::vector<uint8_t> fec_encode(const std::vector<uint8_t>& bits) {
    const Trellis& t = trellis();
    std::vector<uint8_t> out;
    out.reserve(coded_size(bits.size()));
    uint8_t state = 0;
    auto emit = [&](int in) {
        out.push_back(t.out[state][in] >> 1);       // G1
        out.push_back(t.out[state][in] & 1);        // G2
        state = t.next[state][in];
    };
    for (uint8_t b : bits) emit(b ? 1 : 0);
    for (int i = 0; i < K - 1; ++i) emit(0);        // flush to zero state
    return out;
}

std::vector<uint8_t> fec_decode(const std::vector<uint8_t>& coded_bits,
                                const std::vector<uint8_t>* soft) {
    const Trellis& t = trellis();
    if (coded_bits.size() % 2 != 0 || coded_bits.empty()) {
        throw std::invalid_argument("fec_decode: odd/empty coded length");
    }
    const size_t n_steps = coded_bits.size() / 2;

    // path_metric[cur][state]; INT_MAX means unreachable.
    static constexpr int INF = 0x3FFFFFFF;
    std::vector<int> pm(NUM_STATES, INF), pm_next(NUM_STATES, INF);
    // history[step][state] = predecessor state chosen by the survivor path
    // (the input bit itself is recoverable: it is bit K-2 of the state).
    std::vector<std::vector<uint8_t>> hist(n_steps,
                                           std::vector<uint8_t>(NUM_STATES, 0));

    pm[0] = 0; // encoder starts in the zero state
    int r0 = 0, r1 = 0;
    for (size_t step = 0; step < n_steps; ++step) {
        r0 = coded_bits[2 * step] > 0 ? 1 : 0;
        r1 = coded_bits[2 * step + 1] > 0 ? 1 : 0;
        int w0 = 1, w1 = 1; // branch metric weight
        if (soft) {
            uint8_t c0 = (*soft)[2 * step], c1 = (*soft)[2 * step + 1];
            w0 = (c0 == 2) ? 0 : 1; // erasures cost nothing either way
            w1 = (c1 == 2) ? 0 : 1;
        }

        for (int s = 0; s < NUM_STATES; ++s) {
            if (pm[s] >= INF) continue;
            for (int in = 0; in <= 1; ++in) {
                // Hamming distance between received pair and branch output.
                int d = ((r0 ^ (t.out[s][in] >> 1)) ? w0 : 0) +
                        ((r1 ^ (t.out[s][in] & 1)) ? w1 : 0);
                int ns = t.next[s][in];
                int cand = pm[s] + d;
                if (cand < pm_next[ns]) {
                    pm_next[ns] = cand;
                    hist[step][ns] = static_cast<uint8_t>(s); // prev state
                }
            }
        }
        pm.swap(pm_next);
        pm_next.assign(NUM_STATES, INF);
    }

    // Trace back from the best (lowest-metric) end state. The input that
    // caused a transition into `s` is bit K-2 of `s` by construction
    // (ns = ((in << (K-1)) | prev) >> 1 keeps `in` at bit K-2).
    int best = 0;
    for (int s = 1; s < NUM_STATES; ++s) {
        if (pm[s] < pm[best]) best = s;
    }
    std::vector<uint8_t> inputs(n_steps);
    int s = best;
    for (size_t step = n_steps; step-- > 0;) {
        inputs[step] = static_cast<uint8_t>((s >> (K - 2)) & 1);
        s = hist[step][s];
    }
    // Drop the K-1 flush bits.
    inputs.resize(n_steps - (K - 1));
    return inputs;
}

std::vector<uint8_t> bytes_to_bits_fec(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t byte : bytes) {
        for (int i = 0; i < 8; ++i) {
            bits.push_back((byte >> i) & 1); // LSB-first, air convention
        }
    }
    return bits;
}

std::vector<uint8_t> bits_to_bytes_fec(const std::vector<uint8_t>& bits) {
    if (bits.size() % 8 != 0) {
        throw std::invalid_argument("bits_to_bytes_fec: not byte-aligned");
    }
    std::vector<uint8_t> bytes(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) {
        bytes[i / 8] |= static_cast<uint8_t>((bits[i] ? 1u : 0u) << (i % 8));
    }
    return bytes;
}

}
