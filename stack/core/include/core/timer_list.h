#ifndef AETHER_CORE_TIMER_LIST_H
#define AETHER_CORE_TIMER_LIST_H

#include <cstdint>
#include <functional>
#include <vector>

namespace core {

using TimerId = uint32_t;
using TimerCallback = std::function<void()>;

constexpr uint32_t TIMER_NONE = UINT32_MAX;

// Millisecond-resolution timer table driven by an explicit clock.
// The owner polls tick(now_ms) with its loop time; no threads, no real clocks,
// so behavior is fully deterministic and unit-testable.
class TimerList {
public:
    // One-shot when periodic=false. Returns the timer id for cancel().
    TimerId schedule(uint32_t delay_ms, bool periodic, TimerCallback cb);

    void cancel(TimerId id);
    void clear();

    // Fire every due timer at now_ms. Returns ms until next deadline,
    // or TIMER_NONE when no timers are active.
    uint32_t tick(uint32_t now_ms);

    bool empty() const { return entries_.empty(); }

private:
    struct Entry {
        TimerId id = 0;
        uint32_t deadline = 0;
        uint32_t interval = 0; // 0 for one-shot
        bool active = true;
        TimerCallback cb;
    };

    std::vector<Entry> entries_;
    TimerId next_id_ = 1;
    uint32_t last_now_ = 0; // clock anchor so schedule() can be absolute-based
};

}

#endif
