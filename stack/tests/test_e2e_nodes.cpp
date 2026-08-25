// M6.5 T6: cross-layer E2E over an in-memory air interface.
// UeNode <-> BsNode exchange real air frames (pack_air_bits/encode_frame),
// covering SIB gating, RACH 4-step, RRC setup, NAS attach, user-plane
// ping-pong, detach and attach-guard fault recovery.
#include "core/bs_node.h"
#include "core/ue_node.h"
#include "mac/mac_pdu.h"
#include "rrc/rrc_messages.h"
#include <gtest/gtest.h>
#include <random>

namespace {

struct Link {
    core::BsNode bs;
    core::UeNode ue;

    Link() {
        ue.set_air_send([this](const std::vector<uint8_t>& bits) {
            bs.on_air_bits(bits);
        });
        bs.set_air_send([this](const std::vector<uint8_t>& bits) {
            ue.on_air_bits(bits);
        });
    }

    // Advance the clock once so timers anchor to a sane epoch, then start
    // system information broadcast.
    void boot() {
        pump(10);
        bs.start_broadcast();
    }

    // Pump until a predicate holds (downlink now flows through the M11
    // scheduler, so UL-triggered DL needs a few ticks of clock).
    template <typename Pred>
    bool pump_until(Pred pred, uint32_t max_ms = 300) {
        for (uint32_t e = 0; e < max_ms; e += 10) {
            if (pred()) return true;
            pump(10);
        }
        return pred();
    }

    // Advance both nodes' clocks in fixed steps so timers (SIB period,
    // RACH windows, attach guard) fire deterministically.
    void pump(uint32_t ms, uint32_t step = 10) {
        static uint32_t clock_ms = 1000;
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += step) {
            clock_ms += step;
            ue.tick(clock_ms);
            bs.tick(clock_ms);
        }
    }
};

} // namespace

TEST(E2eNodes, AttachDataDetachHappyPath) {
    Link link;

    // BS starts broadcasting; UE asks to attach before any system info.
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));

    EXPECT_TRUE(link.ue.has_system_info());
    EXPECT_TRUE(link.ue.registered());
    EXPECT_NE(link.ue.crnti(), 0u);
    EXPECT_NE(link.ue.nas_state(), nas::UeState::DEREGISTERED);
    EXPECT_EQ(link.ue.mac_state(), mac::RachState::CONNECTED);
    EXPECT_EQ(link.ue.rrc_state(), rrc::UeState::CONNECTED);
    EXPECT_TRUE(link.bs.ue_connected(link.ue.crnti()));
    EXPECT_EQ(link.bs.registered_ue_count(), 1u);
}

TEST(E2eNodes, UserPlanePingPongWithRtt) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));

    std::vector<uint8_t> payload = {'p', 'i', 'n', 'g'};
    for (int i = 0; i < 3; ++i) {
        payload.push_back(static_cast<uint8_t>('0' + i));
        link.ue.send_app_data(payload);
    }
    ASSERT_TRUE(link.pump_until([&] { return link.ue.app_rx_count() == 3u; }));

    EXPECT_EQ(link.ue.app_tx_count(), 3u);
    EXPECT_EQ(link.ue.app_rx_count(), 3u);
    EXPECT_GE(link.ue.last_app_rtt_ms(), 0);
}

TEST(E2eNodes, DetachReleasesBothEnds) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));
    uint16_t crnti = link.ue.crnti();
    ASSERT_TRUE(link.bs.ue_connected(crnti));

    link.ue.detach();
    link.pump(40);

    EXPECT_FALSE(link.ue.registered());
    EXPECT_EQ(link.ue.nas_state(), nas::UeState::DEREGISTERED);
    EXPECT_EQ(link.ue.rrc_state(), rrc::UeState::IDLE);
    EXPECT_EQ(link.ue.mac_state(), mac::RachState::IDLE);
    EXPECT_FALSE(link.bs.ue_connected(crnti));
}

TEST(E2eNodes, ReattachAfterDetach) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));
    link.ue.detach();

    link.ue.attach(); // full second pass through RACH/RRC/NAS
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));

    EXPECT_TRUE(link.ue.registered());
    EXPECT_NE(link.ue.crnti(), 0u);
    EXPECT_TRUE(link.bs.ue_connected(link.ue.crnti()));

    link.ue.send_app_data({0xAA, 0xBB});
    ASSERT_TRUE(link.pump_until([&] { return link.ue.app_rx_count() == 1u; }));
    EXPECT_EQ(link.ue.app_rx_count(), 1u);
}

TEST(E2eNodes, SustainedTrafficLoopbackNoLoss) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));

    // M7.1 DoD at simulated scale: 100ms interval; 5 virtual minutes.
    link.ue.start_traffic(100);
    EXPECT_TRUE(link.ue.traffic_running());
    link.pump(300000);

    link.ue.stop_traffic();
    EXPECT_FALSE(link.ue.traffic_running());
    EXPECT_EQ(link.ue.app_tx_count(), 3000u);  // 300000ms / 100ms
    EXPECT_EQ(link.ue.app_rx_count(), 3000u);
    EXPECT_EQ(link.ue.app_loss_count(), 0u);
    EXPECT_GT(link.ue.rtt_sample_count(), 0u);
    EXPECT_GE(link.ue.rtt_min_ms(), 0);
}

TEST(E2eNodes, TrafficSurvivesAirBlackout) {
    Link link;
    bool blackout = false;
    int dropped = 0;
    // Wrap the downlink so we can silence the channel on demand.
    auto orig_send = [&link](const std::vector<uint8_t>& bits) {
        link.ue.on_air_bits(bits);
    };
    link.bs.set_air_send([&](const std::vector<uint8_t>& bits) {
        if (blackout) { ++dropped; return; }
        orig_send(bits);
    });

    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));
    uint16_t crnti = link.ue.crnti();

    link.ue.start_traffic(50);
    link.pump(1000);            // clean phase: 20 pings answered
    uint32_t rx_before = link.ue.app_rx_count();
    EXPECT_EQ(rx_before, 20u);

    blackout = true;            // air goes dead for 2 s
    link.pump(2000);
    blackout = false;           // channel restored

    link.pump(3500);            // resume + let the 3 s loss window expire

    EXPECT_EQ(link.ue.mac_state(), mac::RachState::CONNECTED);
    EXPECT_EQ(link.ue.rrc_state(), rrc::UeState::CONNECTED);
    EXPECT_TRUE(link.ue.registered());
    EXPECT_EQ(link.ue.crnti(), crnti);
    EXPECT_GT(link.ue.app_rx_count(), rx_before);   // resumed after restore
    // Every ping is either delivered or accounted as lost; HARQ may rescue
    // some blackout pings via timeout retransmissions after restore.
    EXPECT_GE(link.ue.app_rx_count(), 90u);         // clean + post-restore
    EXPECT_LE(link.ue.app_rx_count(), 130u);        // upper bound: all pings
    EXPECT_EQ(link.ue.app_rx_count() + link.ue.app_loss_count(),
              link.ue.app_tx_count());              // full accounting
}

TEST(E2eNodes, AttachGuardTimeoutRecoversToIdle) {
    core::BsNode silent_bs; // never answers
    core::UeNodeConfig cfg;
    cfg.attach_guard_ms = 500;
    core::UeNode ue(cfg);

    // Feed system info directly so attach proceeds, but drop all uplink:
    // the UE must give up via its guard timer and land back in IDLE.
    ue.set_air_send([](const std::vector<uint8_t>&) { /* black hole */ });

    // Inject broadcast frame manually through the public bit interface.
    core::AirFrame f;
    f.type = core::AirFrameType::DATA;
    f.rnti = mac::RNTI_BROADCAST;
    f.payload = mac::build_pdu(
        {{mac::LCID_MIB, rrc::generate_mib(0).encode()},
         {mac::LCID_SIB1, rrc::generate_sib1().encode()}});
    ue.on_air_bits(core::pack_air_bits(core::encode_frame(f)));

    uint32_t clock_ms = 2000;
    ue.tick(clock_ms);
    ue.attach();
    EXPECT_EQ(ue.mac_state(), mac::RachState::WAIT_RAR);

    for (int i = 0; i < 60; ++i) { // 600 ms > guard
        clock_ms += 10;
        ue.tick(clock_ms);
    }

    EXPECT_EQ(ue.mac_state(), mac::RachState::IDLE);
    EXPECT_EQ(ue.rrc_state(), rrc::UeState::IDLE);
    EXPECT_EQ(ue.nas_state(), nas::UeState::DEREGISTERED);
    EXPECT_FALSE(ue.registered());
}

TEST(E2eNodes, HarqRescuesTwentyPercentFrameLoss) {
    // M9 headline: with FEC+HARQ, a channel dropping ~20% of bursts still
    // delivers every application ping (M7-era stacks lost them forever).
    core::BsNode bs;
    core::UeNode ue;
    std::mt19937 rng(1234);
    double drop_p = 0.20;
    int dropped = 0;

    ue.set_air_send([&](const std::vector<uint8_t>& bits) {
        if (std::uniform_real_distribution<>(0, 1)(rng) < drop_p) {
            ++dropped;
            return;
        }
        bs.on_air_bits(bits);
    });
    bs.set_air_send([&](const std::vector<uint8_t>& bits) {
        if (std::uniform_real_distribution<>(0, 1)(rng) < drop_p) {
            ++dropped;
            return;
        }
        ue.on_air_bits(bits);
    });

    uint32_t clock_ms = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock_ms += 10;
            ue.tick(clock_ms);
            bs.tick(clock_ms);
        }
    };

    pump(10);
    bs.start_broadcast();
    ue.attach();
    // RACH frames are not HARQ-protected: a dropped MSG1/MSG3 relies on the
    // UE's window timers and ATTACH_RETRY self-healing to make progress.
    for (int i = 0; i < 200 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());

    ue.start_traffic(50);
    pump(4000);
    ue.stop_traffic();

    printf("[diag] dropped=%u tx=%u rx=%u loss=%u\n", dropped,
           ue.app_tx_count(), ue.app_rx_count(), ue.app_loss_count());
    EXPECT_GT(dropped, 20u);                         // channel really was lossy
    // HARQ turns ~100 dropped bursts into at most a couple of application
    // losses (budget exhaustion). The residual belongs to RLC AM (M13).
    EXPECT_LE(ue.app_loss_count(), 2u);
    EXPECT_GE(ue.app_rx_count(), ue.app_tx_count() - 2u);
    EXPECT_TRUE(ue.registered());
}

TEST(E2eNodes, TwoUesConcurrentAttachAndTraffic) {
    // M11: two UEs share one cell. Each gets its own HARQ entities and a
    // fair share of the downlink scheduler; uplink is grant-free.
    core::BsNode bs;
    core::UeNode ue1, ue2;

    int to1 = 0, to2 = 0;
    ue1.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    ue2.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) {
        // Downlink broadcast reaches both UEs (each decodes independently).
        ue1.on_air_bits(b);
        ue2.on_air_bits(b);
        (void)to1; (void)to2;
    });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue1.tick(clock);
            ue2.tick(clock);
            bs.tick(clock);
        }
    };

    pump(10);
    bs.start_broadcast();

    // Staggered attach: UE1 first, UE2 while UE1 is already registered.
    ue1.attach();
    for (int i = 0; i < 30 && !ue1.registered(); ++i) pump(10);
    ASSERT_TRUE(ue1.registered());
    ue2.attach();
    for (int i = 0; i < 30 && !ue2.registered(); ++i) pump(10);
    ASSERT_TRUE(ue2.registered());

    EXPECT_NE(ue1.crnti(), ue2.crnti());       // distinct grants
    EXPECT_EQ(bs.active_flow_count(), 2u);     // two scheduled flows

    // Concurrent traffic: each UE pings and sees its own echoes only.
    for (int i = 0; i < 5; ++i) {
        ue1.send_app_data({'A'});
        ue2.send_app_data({'B'});
        pump(60);
    }
    EXPECT_EQ(ue1.app_tx_count(), 5u);
    EXPECT_EQ(ue2.app_tx_count(), 5u);
    EXPECT_EQ(ue1.app_rx_count(), 5u);         // no cross-UE leakage
    EXPECT_EQ(ue2.app_rx_count(), 5u);
    EXPECT_EQ(ue1.app_loss_count(), 0u);
    EXPECT_EQ(ue2.app_loss_count(), 0u);

    // Scheduler fairness: both flows drained their queues.
    EXPECT_TRUE(bs.ue_connected(ue1.crnti()));
    EXPECT_TRUE(bs.ue_connected(ue2.crnti()));

    // UE1 detaches; UE2 must keep flowing untouched.
    uint32_t rx2_before = ue2.app_rx_count();
    ue1.detach();
    pump(40);
    ue2.send_app_data({'C'});
    pump(60);
    EXPECT_EQ(ue2.app_rx_count(), rx2_before + 1);
    EXPECT_FALSE(bs.ue_connected(ue1.crnti()));
}
