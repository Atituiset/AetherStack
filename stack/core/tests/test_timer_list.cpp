#include "core/timer_list.h"
#include <gtest/gtest.h>

using namespace core;

TEST(TimerList, OneShotFiresOnceAtDeadline) {
    TimerList timers;
    int fired = 0;
    timers.schedule(100, false, [&] { fired++; });

    EXPECT_EQ(timers.tick(50), 50u); // 50ms remaining
    EXPECT_EQ(fired, 0);
    timers.tick(99);
    EXPECT_EQ(fired, 0);
    timers.tick(100); // deadline reached (<=)
    EXPECT_EQ(fired, 1);
    timers.tick(5000); // must not refire
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(timers.empty());
}

TEST(TimerList, PeriodicFiresRepeatedly) {
    TimerList timers;
    int fired = 0;
    timers.schedule(200, true, [&] { fired++; });

    timers.tick(200);
    EXPECT_EQ(fired, 1);
    timers.tick(400);
    EXPECT_EQ(fired, 2);
    timers.tick(450);
    EXPECT_EQ(fired, 2);
    timers.tick(600);
    EXPECT_EQ(fired, 3);

    auto remaining = timers.tick(601);
    EXPECT_EQ(remaining, 199u); // next at 800
}

TEST(TimerList, CancelPreventsFire) {
    TimerList timers;
    int fired = 0;
    auto id = timers.schedule(50, false, [&] { fired++; });
    timers.cancel(id);
    timers.tick(1000);
    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(timers.empty());
}

TEST(TimerList, CallbackMayCancelOtherTimer) {
    TimerList timers;
    int b_fired = 0;
    TimerId id_b = 0;
    timers.schedule(10, false, [&] { timers.cancel(id_b); });
    id_b = timers.schedule(10, false, [&] { b_fired++; });
    timers.tick(20);
    EXPECT_EQ(b_fired, 0);
    EXPECT_TRUE(timers.empty());
}

TEST(TimerList, OneShotCallbackMayCancelItself) {
    TimerList timers;
    int fired = 0;
    TimerId self = 0;
    self = timers.schedule(10, false, [&] {
        fired++;
        timers.cancel(self);
    });
    timers.tick(20);
    EXPECT_EQ(fired, 1);
}

TEST(TimerList, NextDeadlineIsMinimum) {
    TimerList timers;
    timers.schedule(300, false, [] {});
    timers.schedule(100, false, [] {});
    EXPECT_EQ(timers.tick(0), 100u);
}

TEST(TimerList, TickReturnsMaxWhenEmpty) {
    TimerList timers;
    EXPECT_EQ(timers.tick(0), UINT32_MAX);
}
