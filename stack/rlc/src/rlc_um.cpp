#include "rlc/rlc_um.h"
#include "common/logger.h"
#include <algorithm>

namespace rlc {

std::vector<uint8_t> UmTx::tx(const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu;
    pdu.reserve(sdu.size() + 1);
    pdu.push_back(seq_);
    pdu.insert(pdu.end(), sdu.begin(), sdu.end());
    seq_ = static_cast<uint8_t>(seq_ + 1);
    return pdu;
}

void UmRx::rx(uint32_t now_ms, const std::vector<uint8_t>& pdu) {
    if (pdu.size() < 2) return;
    const uint8_t seq = pdu[0];

    int dist = Wire::ahead(seq, vr_next_, cfg_.window);
    if (dist < 0) {
        // Already delivered or fell out of the reorder window.
        ++duplicates_;
        return;
    }
    auto payload = std::vector<uint8_t>(pdu.begin() + 1, pdu.end());

    if (dist == 0) {
        ready_.push_back(std::move(payload));
        ++vr_next_;
        // Release any contiguous run that was waiting behind the hole.
        for (;;) {
            uint8_t next = Wire::of(vr_next_);
            auto it = buffer_.find(next);
            if (it == buffer_.end()) break;
            ready_.push_back(std::move(it->second));
            buffer_.erase(it);
            ++vr_next_;
        }
        if (buffer_.empty()) timer_running_ = false;
        return;
    }

    // Genuine out-of-order reception: hold and start (or keep) t_reorder.
    if (!buffer_.emplace(seq, std::move(payload)).second) {
        ++duplicates_; // same wire seq already buffered
        return;
    }
    if (!timer_running_) {
        timer_running_ = true;
        timer_deadline_ = now_ms + cfg_.t_reorder_ms;
    }
}

void UmRx::tick(uint32_t now_ms) {
    if (timer_running_ &&
        static_cast<int32_t>(now_ms - timer_deadline_) >= 0) {
        timer_running_ = false;
        on_reorder_timeout();
    }
}

void UmRx::on_reorder_timeout() {
    // Skip every hole between vr_next_ and the closest buffered PDU, then
    // release it; contiguous neighbours follow without further waiting.
    while (!buffer_.empty()) {
        uint8_t best = 0;
        int best_d = -1;
        for (const auto& [s, p] : buffer_) {
            int d = Wire::ahead(s, vr_next_, cfg_.window);
            if (d >= 0 && (best_d < 0 || d < best_d)) {
                best_d = d;
                best = s;
            }
        }
        if (best_d <= 0) break; // nothing usable buffered (should not happen)
        dropped_ += static_cast<uint32_t>(best_d); // each skipped SN is lost
        LOG_WARN(ev::RLC_UM_GAP_SKIP,
                 {{"skipped", std::to_string(best_d)}});
        vr_next_ = static_cast<uint16_t>(vr_next_ + best_d);

        // Deliver from `best` while the buffer stays contiguous.
        for (;;) {
            uint8_t next = Wire::of(vr_next_);
            auto it = buffer_.find(next);
            if (it == buffer_.end()) break;
            ready_.push_back(std::move(it->second));
            buffer_.erase(it);
            ++vr_next_;
        }
        if (buffer_.empty()) break;
        // More holes remain: restart the timer relative to "now" on the
        // next tick — approximate by re-arming with the same window via a
        // fresh deadline assigned by tick().
        timer_running_ = true;
        timer_deadline_ += cfg_.t_reorder_ms;
        return; // let tick() fire the next expiry
    }
}

std::vector<std::vector<uint8_t>> UmRx::poll() {
    std::vector<std::vector<uint8_t>> out(std::make_move_iterator(ready_.begin()),
                                          std::make_move_iterator(ready_.end()));
    ready_.clear();
    return out;
}

}
