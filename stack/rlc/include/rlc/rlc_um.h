#ifndef AETHER_RLC_UM_H
#define AETHER_RLC_UM_H

// M13/M17: RLC Unacknowledged Mode — segmentation, in-order reassembly,
// gap-skip. No ARQ: residual losses are accepted, exactly like real UM.
// That is precisely what makes it the right bearer for media (M17): a
// lossy flood keeps a CONSTANT offered rate instead of amplifying into
// STATUS/retransmission churn the way AM does.
//
// Data PDU : [FI][seq:2 LE][payload]
//            FI bits (b5..b4): 00 complete SDU, 01 first segment,
//            10 last segment, 11 middle. One sequence number per PDU.
//
// Rx: reorder window over the sequence space. In-order PDUs are delivered
// (reassembled) immediately; a gap starts t_reorder, after which the
// missing SNs are skipped (logged) and delivery resumes — a partially
// reassembled SDU spanning a skipped SN is discarded.

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace rlc {

// FI field values (b5..b4), shared layout with the AM header convention.
constexpr uint8_t kUmFiComplete = 0x00;
constexpr uint8_t kUmFiFirst = 0x10;
constexpr uint8_t kUmFiLast = 0x20;
constexpr uint8_t kUmFiMiddle = 0x30;
constexpr uint8_t kUmFiMask = 0x30;

struct UmConfig {
    size_t max_pdu_payload = 256; // segmentation threshold
    uint16_t window = 64;         // reorder window (SN units)
    uint32_t t_reorder_ms = 40;   // how long to wait for a hole to fill
};

class UmTx {
public:
    explicit UmTx(const UmConfig& cfg = {}) : cfg_(cfg) {}

    // Segment `sdu`; one PDU per segment (one for a small SDU).
    std::vector<std::vector<uint8_t>> tx(const std::vector<uint8_t>& sdu);
    void reset() { next_sn_ = 0; }

private:
    UmConfig cfg_;
    uint16_t next_sn_ = 0;
};

class UmRx {
public:
    explicit UmRx(const UmConfig& cfg = {}) : cfg_(cfg) {}

    // Feed one received PDU ([FI][seq:2][payload]).
    void rx(uint32_t now_ms, const std::vector<uint8_t>& pdu);

    // Fire the reorder timer when due (call from the node tick).
    void tick(uint32_t now_ms);

    // SDUs delivered in order since the last poll().
    std::vector<std::vector<uint8_t>> poll();

    size_t buffered() const { return buffer_.size(); }
    uint32_t dropped() const { return dropped_; }     // gap-skipped PDUs
    uint32_t duplicates() const { return duplicates_; }
    void reset();

private:
    struct Held {
        uint8_t fi = 0;
        std::vector<uint8_t> payload;
    };

    // Sequence helper: is `sn` inside (or ahead of) the window starting at
    // vr_next_? Returns the forward distance, -1 when behind/outside.
    int ahead(uint16_t sn) const {
        const int d = static_cast<int16_t>(sn - vr_next_);
        return (d >= 0 && d < static_cast<int>(cfg_.window)) ? d : -1;
    }

    void accept_in_order(uint16_t sn, uint8_t fi,
                         const std::vector<uint8_t>& payload);
    void deliver_ready();
    void on_reorder_timeout(uint32_t now_ms);

    UmConfig cfg_;
    uint16_t vr_next_ = 0;              // next SN to deliver in order
    std::map<uint16_t, Held> buffer_;   // out-of-order hold area
    std::set<uint16_t> seen_;           // duplicate detection (bounded)
    std::deque<std::vector<uint8_t>> ready_;
    // Segment reassembly (fed strictly in SN order):
    bool collecting_ = false;
    std::vector<uint8_t> partial_;
    bool timer_running_ = false;
    uint32_t timer_deadline_ = 0;
    uint32_t dropped_ = 0;
    uint32_t duplicates_ = 0;
};

}
#endif
