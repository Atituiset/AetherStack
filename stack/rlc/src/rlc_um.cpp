#include "rlc/rlc_um.h"
#include "common/logger.h"

namespace rlc {

// ---- UmTx ---------------------------------------------------------------------

std::vector<std::vector<uint8_t>> UmTx::tx(const std::vector<uint8_t>& sdu) {
    std::vector<std::vector<uint8_t>> out;
    if (sdu.empty()) return out;
    const size_t maxp = cfg_.max_pdu_payload;
    const size_t segments = (sdu.size() + maxp - 1) / maxp;
    for (size_t seg = 0; seg < segments; ++seg) {
        auto begin = sdu.begin() + static_cast<long>(seg * maxp);
        auto end = seg == segments - 1
                       ? sdu.end()
                       : sdu.begin() + static_cast<long>((seg + 1) * maxp);
        uint8_t fi = kUmFiComplete;
        if (segments > 1) {
            fi = seg == 0 ? kUmFiFirst
                          : (seg == segments - 1 ? kUmFiLast : kUmFiMiddle);
        }
        std::vector<uint8_t> pdu;
        pdu.reserve(3 + (end - begin));
        pdu.push_back(fi);
        pdu.push_back(static_cast<uint8_t>(next_sn_ & 0xFF));
        pdu.push_back(static_cast<uint8_t>((next_sn_ >> 8) & 0xFF));
        pdu.insert(pdu.end(), begin, end);
        ++next_sn_;
        out.push_back(std::move(pdu));
    }
    return out;
}

// ---- UmRx ---------------------------------------------------------------------

void UmRx::rx(uint32_t now_ms, const std::vector<uint8_t>& pdu) {
    if (pdu.size() < 4) return; // [FI][seq:2] + at least 1 payload byte
    const uint8_t fi = pdu[0] & kUmFiMask;
    const uint16_t sn = static_cast<uint16_t>(pdu[1] | (pdu[2] << 8));

    if (!seen_.insert(sn).second) {
        ++duplicates_;
        return;
    }
    if (seen_.size() > 8192) seen_.erase(seen_.begin()); // bound memory

    const int d = ahead(sn);
    if (d < 0) {
        if (static_cast<int16_t>(sn - vr_next_) >=
            static_cast<int>(cfg_.window)) {
            // Far-ahead jump (link blackout longer than the window): the
            // hole can never fill — resynchronise at this SN instead of
            // dropping the whole stream forever.
            const uint32_t skipped =
                static_cast<uint16_t>(sn - vr_next_);
            dropped_ += skipped;
            vr_next_ = sn;
            buffer_.clear();
            collecting_ = false;
            partial_.clear();
            timer_running_ = false;
            LOG_WARN(ev::RLC_UM_GAP_SKIP,
                     {{"skipped", std::to_string(skipped)}});
            accept_in_order(sn, fi, {pdu.begin() + 3, pdu.end()});
            deliver_ready();
            return;
        }
        ++duplicates_; // behind the window edge: stale
        return;
    }
    if (d == 0) {
        accept_in_order(sn, fi, {pdu.begin() + 3, pdu.end()});
        deliver_ready();
        return;
    }
    // Genuine out-of-order reception: hold and start (or keep) t_reorder.
    buffer_[sn] = {fi, {pdu.begin() + 3, pdu.end()}};
    if (!timer_running_) {
        timer_running_ = true;
        timer_deadline_ = now_ms + cfg_.t_reorder_ms;
    }
}

void UmRx::accept_in_order(uint16_t /*sn*/, uint8_t fi,
                           const std::vector<uint8_t>& payload) {
    switch (fi) {
        case kUmFiComplete:
            ready_.push_back(payload);
            break;
        case kUmFiFirst:
            collecting_ = true;
            partial_ = payload;
            break;
        case kUmFiLast:
            if (collecting_) {
                partial_.insert(partial_.end(), payload.begin(), payload.end());
                ready_.push_back(std::move(partial_));
                collecting_ = false;
                partial_.clear();
            }
            break;
        default: // middle
            if (collecting_) {
                partial_.insert(partial_.end(), payload.begin(), payload.end());
            }
            break;
    }
    ++vr_next_;
}

void UmRx::deliver_ready() {
    while (!buffer_.empty()) {
        auto it = buffer_.find(vr_next_);
        if (it == buffer_.end()) break;
        accept_in_order(it->first, it->second.fi, it->second.payload);
        buffer_.erase(it);
    }
    if (buffer_.empty()) timer_running_ = false;
}

void UmRx::tick(uint32_t now_ms) {
    if (timer_running_ &&
        static_cast<int32_t>(now_ms - timer_deadline_) >= 0) {
        timer_running_ = false;
        on_reorder_timeout(now_ms);
    }
}

void UmRx::on_reorder_timeout(uint32_t now_ms) {
    // The hole is declared lost: skip every SN below the lowest buffered
    // one and resume delivery. A partial SDU spanning a lost segment dies.
    if (buffer_.empty()) return;
    const uint16_t resume = buffer_.begin()->first;
    const uint32_t skipped = static_cast<uint16_t>(resume - vr_next_);
    if (skipped > 0) {
        dropped_ += skipped;
        vr_next_ = resume;
        collecting_ = false;
        partial_.clear();
        LOG_WARN(ev::RLC_UM_GAP_SKIP, {{"skipped", std::to_string(skipped)}});
    }
    deliver_ready();
    if (!buffer_.empty()) {
        // More holes remain: re-arm the timer for the next one.
        timer_running_ = true;
        timer_deadline_ = now_ms + cfg_.t_reorder_ms;
    }
}

std::vector<std::vector<uint8_t>> UmRx::poll() {
    std::vector<std::vector<uint8_t>> out;
    out.assign(std::make_move_iterator(ready_.begin()),
               std::make_move_iterator(ready_.end()));
    ready_.clear();
    return out;
}

void UmRx::reset() {
    vr_next_ = 0;
    buffer_.clear();
    seen_.clear();
    ready_.clear();
    collecting_ = false;
    partial_.clear();
    timer_running_ = false;
    dropped_ = 0;
    duplicates_ = 0;
}

}
