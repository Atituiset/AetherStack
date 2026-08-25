#ifndef AETHER_RLC_RLC_UM_H
#define AETHER_RLC_RLC_UM_H

// M13: RLC Unacknowledged Mode.
//
// Tx: prefixes the SDU with a one-byte sequence number.
// Rx: reorder window over the mod-256 sequence space. In-order PDUs are
// delivered immediately; a gap starts t_reorder, after which the missing
// sequence numbers are skipped (logged) and delivery resumes from the
// closest buffered PDU. No retransmission — residual losses are accepted,
// exactly like real UM.

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace rlc {

class UmTx {
public:
    explicit UmTx(uint8_t first_seq = 0) : seq_(first_seq) {}

    // [seq:1][sdu]
    std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu);

private:
    uint8_t seq_;
};

class UmRx {
public:
    struct Config {
        uint8_t window = 128;       // mod-256 reorder window
        uint32_t t_reorder_ms = 40; // how long to wait for a hole to fill
    };

    UmRx() : UmRx(Config{}) {}
    explicit UmRx(const Config& cfg) : cfg_(cfg) {}

    // Feed one received PDU ([seq][payload]).
    void rx(uint32_t now_ms, const std::vector<uint8_t>& pdu);

    // Fire the reorder timer when due (call from the node tick).
    void tick(uint32_t now_ms);

    // SDUs delivered in order since the last poll().
    std::vector<std::vector<uint8_t>> poll();

    size_t buffered() const { return buffer_.size(); }
    uint32_t dropped() const { return dropped_; }     // gap-skipped SDUs
    uint32_t duplicates() const { return duplicates_; }

private:
    void deliver_ready(std::vector<std::vector<uint8_t>>* sink);
    void on_reorder_timeout();

    struct Wire {
        static uint8_t of(uint16_t abs) { return static_cast<uint8_t>(abs & 0xFF); }
        static int ahead(uint8_t seq, uint16_t base, uint8_t window) {
            int d = static_cast<int>(static_cast<uint8_t>(seq - Wire::of(base)));
            return d < window ? d : -1; // -1 -> outside/at-or-behind the window
        }
    };

    Config cfg_;
    uint16_t vr_next_ = 0;              // next absolute sequence to deliver
    std::map<uint8_t, std::vector<uint8_t>> buffer_; // out-of-order hold area
    std::deque<std::vector<uint8_t>> ready_;
    bool timer_running_ = false;
    uint32_t timer_deadline_ = 0;
    uint32_t dropped_ = 0;
    uint32_t duplicates_ = 0;
};

}
#endif
