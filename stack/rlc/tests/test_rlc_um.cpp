// M13: RLC Unacknowledged Mode — sequence numbers, reorder, gap-skip.
#include "rlc/rlc_um.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>

namespace {

std::vector<uint8_t> sdu_of(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // namespace

TEST(RlcUm, InOrderDeliveryPassThrough) {
    rlc::UmTx tx;
    rlc::UmRx rx;
    for (int i = 0; i < 5; ++i) {
        rx.rx(0, tx.tx(sdu_of("s" + std::to_string(i))));
    }
    auto out = rx.poll();
    ASSERT_EQ(out.size(), 5u);
    EXPECT_EQ(out[0], sdu_of("s0"));
    EXPECT_EQ(out[4], sdu_of("s4"));
}

TEST(RlcUm, ReorderDeliversWhenHoleFills) {
    rlc::UmTx tx;
    rlc::UmRx rx;
    auto p0 = tx.tx(sdu_of("a"));
    auto p1 = tx.tx(sdu_of("b")); // lost in transit
    auto p2 = tx.tx(sdu_of("c"));

    rx.rx(0, p0);
    rx.rx(0, p2); // out of order -> buffered
    rx.rx(10, p1); // hole filled
    auto out = rx.poll();
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], sdu_of("a"));
    EXPECT_EQ(out[1], sdu_of("b"));
    EXPECT_EQ(out[2], sdu_of("c"));
}

TEST(RlcUm, GapSkippedAfterReorderTimeout) {
    rlc::UmTx tx;
    rlc::UmRx rx;
    (void)tx.tx(sdu_of("a")); // seq 0 lost forever
    auto p1 = tx.tx(sdu_of("b"));
    auto p2 = tx.tx(sdu_of("c"));

    rx.rx(0, p1);
    rx.rx(0, p2);
    EXPECT_TRUE(rx.poll().empty());

    rx.tick(39); // just before expiry
    EXPECT_TRUE(rx.poll().empty());
    rx.tick(41); // t_reorder elapsed -> skip seq 0
    auto out = rx.poll();
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], sdu_of("b"));
    EXPECT_EQ(out[1], sdu_of("c"));
    EXPECT_EQ(rx.dropped(), 1u);
}

TEST(RlcUm, DuplicatesIgnored) {
    rlc::UmTx tx;
    rlc::UmRx rx;
    auto p = tx.tx(sdu_of("x"));
    rx.rx(0, p);
    rx.rx(0, p);
    auto out = rx.poll();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(rx.duplicates(), 1u);
}

TEST(RlcUm, MalformedShortPduRejected) {
    rlc::UmRx rx;
    rx.rx(0, {0x05}); // header only, no payload
    EXPECT_TRUE(rx.poll().empty());
}
