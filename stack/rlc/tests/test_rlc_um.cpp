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
        for (const auto& pdu : tx.tx(sdu_of("s" + std::to_string(i)))) {
            rx.rx(0, pdu);
        }
    }
    auto out = rx.poll();
    ASSERT_EQ(out.size(), 5u);
    EXPECT_EQ(out[0], sdu_of("s0"));
    EXPECT_EQ(out[4], sdu_of("s4"));
}

TEST(RlcUm, ReorderDeliversWhenHoleFills) {
    rlc::UmTx tx;
    rlc::UmRx rx;
    auto p0 = tx.tx(sdu_of("a"))[0];
    auto p1 = tx.tx(sdu_of("b"))[0]; // lost in transit
    auto p2 = tx.tx(sdu_of("c"))[0];

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
    auto p1 = tx.tx(sdu_of("b"))[0];
    auto p2 = tx.tx(sdu_of("c"))[0];

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
    auto p = tx.tx(sdu_of("x"))[0];
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

TEST(RlcUm, SegmentationAndReassembly) {
    // M17: SDUs above max_pdu_payload segment (voice/video bearers).
    rlc::UmTx tx;
    rlc::UmRx rx;
    std::vector<uint8_t> sdu(600, 0x5A);
    auto pdus = tx.tx(sdu);
    ASSERT_EQ(pdus.size(), 3u); // 256 + 256 + 88
    EXPECT_EQ(pdus[0][0], rlc::kUmFiFirst);
    EXPECT_EQ(pdus[1][0], rlc::kUmFiMiddle);
    EXPECT_EQ(pdus[2][0], rlc::kUmFiLast);
    for (const auto& pdu : pdus) rx.rx(0, pdu);
    auto out = rx.poll();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], sdu);
}

TEST(RlcUm, LostSegmentKillsWholeSdu) {
    rlc::UmTx tx;
    rlc::UmRx rx;
    std::vector<uint8_t> sdu(600, 0x33);
    auto pdus = tx.tx(sdu);
    rx.rx(0, pdus[0]);   // first
    rx.rx(0, pdus[2]);   // last, middle lost -> held (sn ahead)
    EXPECT_TRUE(rx.poll().empty());
    rx.tick(100);        // reorder timeout: skip the hole, partial dies
    EXPECT_TRUE(rx.poll().empty());
    EXPECT_GE(rx.dropped(), 1u);
}
