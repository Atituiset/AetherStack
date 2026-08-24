#include "core/timer_list.h"
#include <algorithm>

namespace core {

TimerId TimerList::schedule(uint32_t delay_ms, bool periodic, TimerCallback cb) {
    Entry e;
    e.id = next_id_++;
    e.deadline = delay_ms;
    e.interval = periodic ? delay_ms : 0;
    e.cb = cb;
    entries_.push_back(std::move(e));
    return entries_.back().id;
}

void TimerList::cancel(TimerId id) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [id](const Entry& e) { return e.id == id; });
    if (it != entries_.end()) {
        entries_.erase(it);
    }
}

void TimerList::clear() {
    entries_.clear();
}

uint32_t TimerList::tick(uint32_t now_ms) {
    // Snapshot due ids first; callbacks are free to cancel themselves or others.
    std::vector<TimerId> due_ids;
    for (const auto& e : entries_) {
        if (static_cast<int32_t>(now_ms - e.deadline) >= 0) {
            due_ids.push_back(e.id);
        }
    }

    for (TimerId id : due_ids) {
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [id](const Entry& e) { return e.id == id; });
        if (it == entries_.end()) continue; // cancelled earlier this tick

        Entry snapshot = *it; // survive mutation from callbacks
        if (snapshot.interval > 0) {
            it->deadline += snapshot.interval;
            if (static_cast<int32_t>(now_ms - it->deadline) >= 0) {
                // Fell far behind (e.g. after a stall): resync instead of burst-firing.
                it->deadline = now_ms + snapshot.interval;
            }
            snapshot.cb();
        } else {
            entries_.erase(it); // one-shot gone before its callback runs
            snapshot.cb();
        }
    }

    uint32_t next = TIMER_NONE;
    for (const auto& e : entries_) {
        next = std::min(next, e.deadline - now_ms);
    }
    return next;
}

}
