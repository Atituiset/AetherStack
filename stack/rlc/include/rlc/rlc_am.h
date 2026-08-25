#ifndef AETHER_RLC_RLC_AM_H
#define AETHER_RLC_RLC_AM_H

// M13: RLC Acknowledged Mode — segmentation, in-order reassembly and ARQ.
//
// Data PDU : [0x80 | FI | P] [seq:2 LE] [payload]
//            D/C=1; FI bits (b5..b4): 00 complete SDU, 01 first segment,
//            10 last segment, 11 middle; P = polling bit (request STATUS).
// STATUS   : [0x40] [ack_sn:2 LE] [nack_count:1] [nack_seq:2 LE]...
//            ack_sn acknowledges every SN below it (contiguous reception);
//            the nack list names the detected holes.
//
// The receiver delivers strictly in order. An SN arriving ahead of the
// window edge records the skipped SNs as holes and triggers a STATUS; the
// affected PDUs wait in a reorder hold area. The transmitter keeps every
// sent PDU until it falls below the cumulative ACK and resends nacked PDUs
// on demand. Segments carry their own SN, so the retransmission unit is
// one PDU (= one segment).

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace rlc {

struct AmConfig {
    size_t max_pdu_payload = 256; // segmentation threshold
    size_t tx_buffer_limit = 512; // backpressure bound (unacked PDUs)
    uint8_t poll_every = 4;       // poll bit on every Nth new PDU
    uint32_t t_poll_ms = 120;     // probe-retransmit silence window (0=off)
    size_t probe_batch = 8;       // max PDUs re-sent per probe round
};

constexpr uint8_t kAmDcData = 0x80;
constexpr uint8_t kAmDcControl = 0x40;
constexpr uint8_t kAmFiMask = 0x30;
constexpr uint8_t kAmPollBit = 0x08;

class AmTx {
public:
    explicit AmTx(const AmConfig& cfg = {}) : cfg_(cfg) {}

    // Segment `sdu` and return the PDUs to hand to the lower layer.
    std::vector<std::vector<uint8_t>> tx(uint32_t now_ms,
                                         const std::vector<uint8_t>& sdu);

    // Process a STATUS PDU. Returns buffered PDUs that must be resent now
    // (they stay buffered until the cumulative ACK covers them). Nack-driven
    // retransmission is rate-limited to one burst per t_poll_ms so repeated
    // statuses cannot flood the lower layer.
    std::vector<std::vector<uint8_t>> on_status(
        uint32_t now_ms, const std::vector<uint8_t>& status);

    size_t unacked() const { return tx_buffer_.size(); }
    uint32_t retx_count() const { return retxs_; }
    void reset(); // fresh bearer: drop everything, restart sequence numbers

    // Liveness probe: when unacked data has sat silent past t_poll_ms (the
    // peer's STATUS may itself have been lost), resend EVERY unacked PDU in
    // SN order. Returns the PDUs to transmit, or empty. Safe every tick.
    std::vector<std::vector<uint8_t>> tick(uint32_t now_ms);

private:
    std::vector<uint8_t> build_pdu(uint16_t seq, uint8_t fi_with_poll,
                                   const std::vector<uint8_t>& payload) const;

    AmConfig cfg_;
    std::map<uint16_t, std::vector<uint8_t>> tx_buffer_; // SN -> PDU bytes
    uint16_t next_sn_ = 0;
    uint8_t since_poll_ = 0;
    uint32_t retxs_ = 0;
    int64_t last_progress_ms_ = -1;  // last tx / retx / status reception
    int64_t last_nack_burst_ms_ = -1; // last nack-driven retx burst
};

class AmRx {
public:
    struct Outcome {
        std::vector<std::vector<uint8_t>> delivered; // in-order whole SDUs
        bool status_needed = false;                  // call build_status()
    };

    explicit AmRx(const AmConfig& cfg = {}) : cfg_(cfg) {}

    Outcome rx(const std::vector<uint8_t>& pdu);
    std::vector<uint8_t> build_status() const;

    uint16_t vr_next() const { return vr_next_; }
    size_t gaps_reported() const { return gaps_; }
    size_t unrecovered_drops() const { return overflows_; }
    void reset(); // fresh bearer: drop everything, restart sequence numbers

private:
    struct Held {
        uint8_t fi = 0;
        std::vector<uint8_t> payload;
    };

    void accept_in_order(uint16_t sn, uint8_t fi,
                         const std::vector<uint8_t>& payload, Outcome& out);

    AmConfig cfg_;
    uint16_t vr_next_ = 0;              // lowest SN not yet received in order
    std::set<uint16_t> seen_;           // duplicate detection
    std::set<uint16_t> holes_;          // reported-missing SNs awaiting retx
    std::map<uint16_t, Held> reorder_;  // out-of-order hold area
    // Segment reassembly (fed strictly in SN order):
    bool collecting_ = false;
    std::vector<uint8_t> partial_;
    size_t gaps_ = 0;
    size_t overflows_ = 0;
};

}
#endif
