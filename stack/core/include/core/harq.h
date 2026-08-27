#ifndef AETHER_CORE_HARQ_H
#define AETHER_CORE_HARQ_H

// M9: link reliability — stop-and-wait HARQ with multiple parallel
// processes, Chase-combining soft memory on the RX side, and FEC
// (convolutional code) inside the retransmission loop.
//
// Transport block path (TX):
//   mac_pdu bytes -> crc_attach -> fec_encode -> [harq header | coded bytes]
//
// Feedback: LCID 60 control PDU [proc][ack] carried on the opposite
// direction inside a regular DATA frame. A missing ACK (timeout or NACK)
// triggers a retransmission of the identical coded block; the receiver
// majority-combines the two attempts before decoding again.

#include "core/timer_list.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace core {

// ---- wire format -----------------------------------------------------------

struct HarqHeader {
    uint8_t proc = 0;      // HARQ process id
    uint8_t ndi = 1;       // new-data indicator (flips per new transport block)
    uint8_t rv = 0;        // retransmission counter (0 = first attempt)
    uint16_t len = 0;      // coded byte length (incl. alignment padding)
    uint16_t info_len = 0; // transport-block byte length (mac_pdu + CRC)

    static constexpr size_t kSize = 7;
    std::vector<uint8_t> pack() const;
    static bool unpack(const std::vector<uint8_t>& buf, HarqHeader& out);
};

// Coded bits that actually carry information (excludes alignment padding).
inline size_t valid_coded_bits(const HarqHeader& h) {
    return (static_cast<size_t>(h.info_len) * 8 + 6) * 2; // + K-1 tail bits
}

// ---- TX side ----------------------------------------------------------------

struct HarqTxConfig {
    uint8_t num_processes = 8;
    uint8_t max_retx = 3;
    // Must comfortably exceed the worst-case round trip (channel latency +
    // peer processing + queueing) or every busy-but-healthy block gets
    // retransmitted, doubling process occupancy and feeding a congestion
    // spiral. Measured loaded RTT at media rates is 100-250 ms.
    uint32_t ack_timeout_ms = 250;
};

class HarqTx {
public:
    struct Event {
        enum Type { NEW_TX, RETX } type = NEW_TX;
        uint8_t proc = 0;
        uint8_t ndi = 1;
        std::vector<uint8_t> coded; // coded+packed payload incl. harq header
    };

    explicit HarqTx(const HarqTxConfig& cfg = {});

    // Submit a new transport block. Returns nullopt when all processes are
    // busy (caller retries on the next tick).
    std::optional<Event> send(const std::vector<uint8_t>& mac_pdu);

    // Advance the internal clock (must precede feedback handling).
    void advance(uint32_t now_ms);

    // Re-arm transmissions whose ACK timed out.
    std::vector<Event> poll_timeouts(uint32_t now_ms);

    void on_ack(uint8_t proc);
    // NACK-driven immediate retransmission; the returned event must go on
    // the air right away. nullopt when the budget is exhausted.
    std::optional<Event> on_nack(uint8_t proc);

    void reset(); // drop everything (connection teardown)

    size_t in_flight() const {
        size_t n = 0;
        for (const auto& p : procs_) n += p.busy ? 1 : 0;
        return n;
    }
    size_t num_processes() const { return procs_.size(); }

private:
    // Core retransmission logic; appends to `sink`.
    void retx(uint8_t proc, const char* reason, std::vector<Event>* sink);

    struct Proc {
        bool busy = false;
        uint8_t ndi = 1;
        uint8_t retx_count = 0;
        uint32_t deadline = 0;
        std::vector<uint8_t> coded;      // full frame payload (header+coded)
        std::vector<uint8_t> crc_block;  // for logging only
    };
    std::vector<Event> retx(uint8_t proc, const char* reason);

    HarqTxConfig cfg_;
    std::vector<Proc> procs_;
    uint8_t next_proc_ = 0;
    uint32_t last_now_ = 0;
};

// ---- RX side ----------------------------------------------------------------

class HarqRx {
public:
    // Feed one received transmission. Returns true when a valid MAC PDU is
    // ready in `mac_pdu` (CRC verified). `need_feedback` reports whether an
    // ACK/NACK must be sent back and `last_ack` carries its value.
    struct Result {
        bool delivered = false;
        bool need_feedback = false;
        bool ack = false;
        uint8_t proc = 0;   // HARQ process the frame belonged to
        std::vector<uint8_t> mac_pdu;
    };
    Result receive(const std::vector<uint8_t>& frame_payload);

    // Number of blocks currently saved for chase combining.
    size_t combining() const { return soft_.size(); }

    void reset(); // drop all soft buffers (connection teardown)

private:
    struct SoftBuf {
        uint8_t ndi = 1;
        std::vector<uint8_t> bits; // previous attempt's coded bits
    };
    std::vector<uint8_t>* find_soft(uint8_t proc);
    std::vector<SoftBuf> soft_{8}; // one slot per HARQ process id
};

// True when the payload carries a HARQ header (magic byte check). Frames
// that fail this test are legacy raw MAC PDUs (broadcasts, ACK control).
bool is_harq_framed(const std::vector<uint8_t>& buf);

// ---- codec helpers ----------------------------------------------------------

// mac_pdu -> packed frame payload ([harq header][coded bytes]).
std::vector<uint8_t> link_encode(const std::vector<uint8_t>& mac_pdu,
                                 uint8_t proc, uint8_t ndi);

// Inverse of link_encode at the bit level: returns the raw coded bits of the
// payload region so a receiver can merge attempts before decoding. Only the
// first valid_coded_bits(header) bits are meaningful; trailing bits are
// byte-alignment padding.
std::vector<uint8_t> link_payload_bits(const std::vector<uint8_t>& frame_payload);

// Decode coded bits into a CRC-verified MAC PDU. `info_len` is the original
// transport-block byte length carried in the header.
bool link_decode_bits(const std::vector<uint8_t>& coded_bits,
                      size_t info_len, std::vector<uint8_t>& mac_pdu);

}

#endif
