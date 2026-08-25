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
