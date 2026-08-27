// M13: RLC Acknowledged Mode — segmentation, ARQ status/retx, zero loss.
#include "rlc/rlc_am.h"
#include <gtest/gtest.h>

namespace {

std::vector<uint8_t> make_sdu(size_t n, uint8_t fill) {
    return std::vector<uint8_t>(n, fill);
}

// Deliver every PDU in order; returns what the receiver produced.
rlc::AmRx::Outcome deliver_all(rlc::AmRx& rx,
                               const std::vector<std::vector<uint8_t>>& pdus) {
    rlc::AmRx::Outcome out;
    for (const auto& p : pdus) {
        auto o = rx.rx(p);
        out.delivered.insert(out.delivered.end(), o.delivered.begin(),
                             o.delivered.end());
        out.status_needed = out.status_needed || o.status_needed;
    }
    return out;
}

} // namespace

TEST(RlcAm, SmallSduRoundTripSinglePdu) {
    rlc::AmTx tx;
    rlc::AmRx rx;
    auto pdus = tx.tx(/*now=*/0, make_sdu(32, 0xAB));
    ASSERT_EQ(pdus.size(), 1u);
    auto out = deliver_all(rx, pdus);
    EXPECT_FALSE(out.status_needed);
    ASSERT_EQ(out.delivered.size(), 1u);
    EXPECT_EQ(out.delivered[0], make_sdu(32, 0xAB));
}

TEST(RlcAm, SegmentationAndReassembly) {
    rlc::AmTx tx; // default max_pdu_payload = 256
    rlc::AmRx rx;
    auto sdu = make_sdu(700, 0x5A); // 3 segments: 256 + 256 + 188
    auto pdus = tx.tx(/*now=*/0, sdu);
    ASSERT_EQ(pdus.size(), 3u);

    auto out = deliver_all(rx, pdus);
    ASSERT_EQ(out.delivered.size(), 1u);
    EXPECT_EQ(out.delivered[0], sdu);
}

TEST(RlcAm, LostPduRecoveredViaStatus) {
    rlc::AmConfig cfg;
    cfg.poll_every = 255; // no polls: only genuine losses trigger STATUS
    rlc::AmTx tx{cfg};
    rlc::AmRx rx;
    auto pdus = tx.tx(/*now=*/0, make_sdu(600, 0x11)); // 3 segments (sn 0..2)

    // Receiver gets sn0 and sn2; sn1 is lost.
    rlc::AmRx::Outcome acc;
    auto o0 = rx.rx(pdus[0]);
    auto o2 = rx.rx(pdus[2]);
    acc.status_needed = o0.status_needed || o2.status_needed;

    // sn2 arrives ahead of vr_next -> hole reported, nothing delivered yet.
    EXPECT_TRUE(acc.status_needed);
    EXPECT_TRUE(acc.delivered.empty());

    // STATUS reports ack=1 with nack=[1].
    auto status = rx.build_status();
    EXPECT_EQ(status[0], rlc::kAmDcControl);
    const uint16_t ack = static_cast<uint16_t>(status[1] | (status[2] << 8));
    EXPECT_EQ(ack, 1u);
    EXPECT_EQ(status[3], 1); // one nack

    // Transmitter resends the nacked PDU (still buffered).
    auto retx = tx.on_status(/*now=*/0, status);
    ASSERT_EQ(retx.size(), 1u);
    EXPECT_EQ(retx[0], pdus[1]);

    // Delivery completes in order and the hole clears.
    auto final = rx.rx(retx[0]);
    ASSERT_EQ(final.delivered.size(), 1u);
    EXPECT_EQ(final.delivered[0], make_sdu(600, 0x11));
}

TEST(RlcAm, CumulativeAckReleasesBuffer) {
    rlc::AmTx tx;
    rlc::AmRx rx;
    auto pdus = tx.tx(/*now=*/0, make_sdu(100, 0x22));
    deliver_all(rx, pdus);
    // A plain ACK covering everything empties the tx buffer.
    std::vector<uint8_t> status = {rlc::kAmDcControl, 0x01, 0x00, 0};
    EXPECT_TRUE(tx.on_status(/*now=*/0, status).empty());
    EXPECT_EQ(tx.unacked(), 0u);
}

TEST(RlcAm, DuplicatePduIgnored) {
    rlc::AmTx tx;
    rlc::AmRx rx;
    auto pdus = tx.tx(/*now=*/0, make_sdu(50, 0x33));
    auto first = rx.rx(pdus[0]);
    auto dup = rx.rx(pdus[0]);
    EXPECT_EQ(first.delivered.size(), 1u);
    EXPECT_TRUE(dup.delivered.empty());
}

TEST(RlcAm, PollBitRequestsStatusWithoutLoss) {
    rlc::AmConfig cfg;
    cfg.poll_every = 1; // poll on every PDU
    rlc::AmTx tx{cfg};
    rlc::AmRx rx;
    auto pdus = tx.tx(/*now=*/0, make_sdu(10, 0x44));
    auto out = rx.rx(pdus[0]);
    EXPECT_TRUE(out.status_needed);      // poll honoured
    EXPECT_FALSE(rx.build_status()[3]);  // but no nacks
}

TEST(RlcAm, WindowFullRefusesNewSduInsteadOfShedding) {
    // M16.1: shedding the oldest unacked PDU strands the peer's reassembly
    // on an SN nobody will retransmit (permanent wedge). The transmitter
    // must refuse NEW SDUs while the window is full and keep the old ones.
    rlc::AmConfig cfg;
    cfg.tx_buffer_limit = 4;
    cfg.t_poll_ms = 1;
    rlc::AmTx tx{cfg};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(tx.tx(0, make_sdu(10, static_cast<uint8_t>(i))).size(), 1u);
    }
    EXPECT_EQ(tx.unacked(), 4u);
    EXPECT_TRUE(tx.tx(0, make_sdu(10, 0x55)).empty()); // refused, not shed
    EXPECT_TRUE(tx.tx(0, make_sdu(10, 0x66)).empty());
    EXPECT_EQ(tx.tx_dropped(), 2u);
    EXPECT_EQ(tx.unacked(), 4u);
    // The oldest SNs are still buffered: the probe resends SN 0..3.
    auto probe = tx.tick(10);
    ASSERT_EQ(probe.size(), 4u);
    for (size_t i = 0; i < probe.size(); ++i) {
        EXPECT_EQ(probe[i][1], static_cast<uint8_t>(i)); // SN i (LE low byte)
    }
}

TEST(RlcAm, ReorderingTimeoutSkipsStaleHoles) {
    // M16.1: a hole that stops making progress past t_reorder_ms is declared
    // lost; in-order delivery resynchronises instead of wedging forever.
    rlc::AmConfig cfg;
    cfg.poll_every = 255;
    cfg.t_reorder_ms = 100;
    rlc::AmRx rx{cfg};

    rlc::AmTx tx;
    auto pdu0 = tx.tx(0, make_sdu(10, 0xA0)); // sn0
    auto pdu1 = tx.tx(0, make_sdu(10, 0xB0)); // sn1 (will be "lost")
    auto pdu2 = tx.tx(0, make_sdu(10, 0xC0)); // sn2
    (void)pdu1;

    EXPECT_EQ(rx.rx(pdu0[0]).delivered.size(), 1u);
    auto held = rx.rx(pdu2[0]); // hole at sn1, sn2 held
    EXPECT_TRUE(held.delivered.empty());
    EXPECT_EQ(rx.vr_next(), 1u);

    // Progress baseline anchors on the first tick; the hole ages from there.
    EXPECT_TRUE(rx.tick(0).delivered.empty());
    EXPECT_TRUE(rx.tick(50).delivered.empty()); // within the deadline
    auto out = rx.tick(150);                    // deadline exceeded
    EXPECT_EQ(out.delivered.size(), 1u);        // sn2 freed by the skip
    EXPECT_EQ(out.delivered[0], make_sdu(10, 0xC0));
    EXPECT_TRUE(out.status_needed);             // peer learns the new ack_sn
    EXPECT_EQ(rx.vr_next(), 3u);
    EXPECT_EQ(rx.build_status()[3], 0);         // holes cleared

    // The "lost" PDU arriving late is dropped as stale, not delivered twice.
    EXPECT_TRUE(rx.rx(pdu1[0]).delivered.empty());
    EXPECT_EQ(rx.vr_next(), 3u);
}

