// M9b: HARQ unit tests — SAW processes, timeout retransmission, chase
// combining under mixed frame drops and bit flips.
#include "core/harq.h"
#include "mac/mac_pdu.h"
#include <gtest/gtest.h>
#include <random>

using namespace core;

namespace {

struct Channel {
    // Deterministic impairment: whole-frame drops + random bit flips.
    double drop_p = 0.0;
    double flip_p = 0.0;
    uint32_t seed = 1;

    bool pass(std::vector<uint8_t>& payload, std::mt19937& rng) {
        if (drop_p > 0 &&
            std::uniform_real_distribution<>(0, 1)(rng) < drop_p) {
            return false;
        }
        if (flip_p > 0) {
            for (auto& b : payload) {
                if (std::uniform_real_distribution<>(0, 1)(rng) < flip_p) {
                    b ^= 1;
                }
            }
        }
        return true;
    }
};

static int g_busy_fail = 0, g_rounds_fail = 0;

// Drives one transport block through TX -> impaired air -> RX with feedback
// until it is delivered or the retry budget dies. Returns delivery status.
bool pump_block(HarqTx& tx, HarqRx& rx, Channel& air_fb, Channel& air_data,
                uint32_t& now, const std::vector<uint8_t>& tb,
                std::mt19937& rng) {
    auto ev = tx.send(tb);
    if (!ev.has_value()) { ++g_busy_fail; return false; }

    for (int round = 0; round < 16; ++round) {
        auto payload = ev->coded;
        bool delivered_now = false;
        if (air_data.pass(payload, rng)) {
            auto res = rx.receive(payload);
            delivered_now = res.delivered;
            if (res.need_feedback) {
                std::vector<uint8_t> fb{static_cast<uint8_t>(ev->proc),
                                        static_cast<uint8_t>(res.ack ? 1 : 0)};
                if (air_fb.pass(fb, rng)) {
                    if (res.ack) tx.on_ack(ev->proc);
                    else tx.on_nack(ev->proc);
                }
            }
            if (delivered_now) {
                // A lost ACK leaves the process hanging until its timeout;
                // simulate that passage of time so the caller starts clean.
                now += 120;
                tx.poll_timeouts(now);
                return true;
            }
        }
        // Advance time so the ACK timeout can fire across lost frames.
        now += 40;
        auto evs = tx.poll_timeouts(now);
        if (!evs.empty()) ev = evs[0];
    }
    ++g_rounds_fail;
    now += 200;
    tx.poll_timeouts(now); // release anything left hanging
    return false;
}

} // namespace

TEST(Harq, CleanExchangeDeliversAndAcks) {
    HarqTx tx;
    HarqRx rx;
    std::vector<uint8_t> tb = {0xCA, 0xFE, 0xBA, 0xBE};

    auto ev = tx.send(tb);
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->type, HarqTx::Event::NEW_TX);

    auto res = rx.receive(ev->coded);
    ASSERT_TRUE(res.need_feedback);
    EXPECT_TRUE(res.delivered);
    EXPECT_TRUE(res.ack);
    EXPECT_EQ(res.mac_pdu, tb);
    tx.on_ack(ev->proc);
    EXPECT_EQ(tx.in_flight(), 0u);
}

TEST(Harq, LegacyPayloadNotMisreadAsHarq) {
    HarqRx rx;
    // Raw MAC PDU without the magic byte must not be consumed by HARQ.
    std::vector<uint8_t> legacy = mac::build_pdu({{mac::LCID_SIB1, {0x01}}});
    auto res = rx.receive(legacy);
    EXPECT_FALSE(res.need_feedback);
    EXPECT_FALSE(res.delivered);
    EXPECT_TRUE(is_harq_framed(legacy) == false);
}

TEST(Harq, BitErrorsTriggerNackThenCleanRetxDelivers) {
    HarqTx tx;
    HarqRx rx;
    std::vector<uint8_t> tb(40);
    for (size_t i = 0; i < tb.size(); ++i) tb[i] = static_cast<uint8_t>(i * 13);

    auto first = tx.send(tb);
    ASSERT_TRUE(first.has_value());

    // Corrupt the coded region beyond FEC correction. (A damaged *header*
    // cannot even be parsed; that case is covered by the timeout test below.)
    auto corrupted = first->coded;
    for (size_t i = HarqHeader::kSize; i < corrupted.size(); ++i) {
        corrupted[i] ^= 0xFF;
    }
    auto r1 = rx.receive(corrupted);
    ASSERT_TRUE(r1.need_feedback);
    EXPECT_FALSE(r1.delivered);
    EXPECT_FALSE(r1.ack);
    tx.on_nack(first->proc);

    // Identical Chase retransmission: combined with attempt-1 bits, decodes.
    auto r2 = rx.receive(first->coded);
    EXPECT_TRUE(r2.delivered);
    EXPECT_EQ(r2.mac_pdu, tb);
}

TEST(Harq, DestroyedHeaderReliesOnTxTimeout) {
    HarqTxConfig cfg;
    cfg.ack_timeout_ms = 40;
    HarqTx tx(cfg);
    HarqRx rx;

    auto ev = tx.send({0x55, 0x66});
    ASSERT_TRUE(ev.has_value());
    auto smashed = ev->coded;
    for (int i = 0; i < 3; ++i) smashed[i] ^= 0xFF; // destroy the header

    auto r = rx.receive(smashed);
    EXPECT_FALSE(r.need_feedback); // unparseable: silence, no feedback

    uint32_t now = 500;
    auto evs = tx.poll_timeouts(now + 100);
    ASSERT_EQ(evs.size(), 1u);     // timeout saves the day
    auto r2 = rx.receive(evs[0].coded);
    EXPECT_TRUE(r2.delivered);
}

TEST(Harq, TimeoutRetransmitsAfterFrameDrop) {
    HarqTxConfig cfg;
    cfg.ack_timeout_ms = 80;
    HarqTx tx(cfg);

    auto ev = tx.send({0x11, 0x22});
    ASSERT_TRUE(ev.has_value());

    EXPECT_TRUE(tx.poll_timeouts(50).empty()); // before deadline

    auto late = tx.poll_timeouts(200); // past deadline
    ASSERT_EQ(late.size(), 1u);
    EXPECT_EQ(late[0].type, HarqTx::Event::RETX);
    EXPECT_EQ(late[0].proc, ev->proc);
    EXPECT_EQ(late[0].ndi, ev->ndi);   // same NDI -> receiver combines
    EXPECT_EQ(late[0].coded, ev->coded);

    tx.on_ack(ev->proc);
    EXPECT_EQ(tx.in_flight(), 0u);
}

TEST(Harq, RetxBudgetExhaustionFreesProcess) {
    HarqTxConfig cfg;
    cfg.max_retx = 2;
    cfg.ack_timeout_ms = 10;
    HarqTx tx(cfg);

    auto ev = tx.send({0x01});
    ASSERT_TRUE(ev.has_value());

    uint32_t now = 1000;
    for (int i = 0; i < 6; ++i) { // far beyond budget
        now += 20;
        if (tx.poll_timeouts(now).empty()) break;
    }
    EXPECT_EQ(tx.in_flight(), 0u); // process released
    auto again = tx.send({0x02});  // and reusable
    EXPECT_TRUE(again.has_value());
}

TEST(Harq, EndToEndMixedImpairmentZeroDeliveryLoss) {
    // The M9 headline scenario: heavy frame drops plus bit flips on both the
    // data path and the feedback path — every block must still land.
    HarqTx tx;
    HarqRx rx;
    Channel data_ch{0.30, 0.04, 99};
    Channel fb_ch{0.20, 0.00, 7};
    std::mt19937 rng(data_ch.seed);

    uint32_t now = 1000;
    int delivered = 0;
    const int kBlocks = 40;

    for (int i = 0; i < kBlocks; ++i) {
        std::vector<uint8_t> tb(48);
        for (size_t k = 0; k < tb.size(); ++k) {
            tb[k] = static_cast<uint8_t>(i * 7 + k * 3);
        }
        if (pump_block(tx, rx, fb_ch, data_ch, now, tb, rng)) ++delivered;
    }
    EXPECT_EQ(delivered, kBlocks);
    EXPECT_EQ(g_busy_fail, 0);
    EXPECT_EQ(g_rounds_fail, 0);
}
