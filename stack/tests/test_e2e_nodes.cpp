// M6.5 T6: cross-layer E2E over an in-memory air interface.
// UeNode <-> BsNode exchange real air frames (pack_air_bits/encode_frame),
// covering SIB gating, RACH 4-step, RRC setup, NAS attach, user-plane
// ping-pong, detach and attach-guard fault recovery.
#include "core/bs_node.h"
#include "core/ue_node.h"
#include "mac/mac_pdu.h"
#include "rrc/rrc_messages.h"
#include <gtest/gtest.h>

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
    ASSERT_TRUE(link.ue.registered());

    std::vector<uint8_t> payload = {'p', 'i', 'n', 'g'};
    for (int i = 0; i < 3; ++i) {
        payload.push_back(static_cast<uint8_t>('0' + i));
        link.ue.send_app_data(payload);
    }

    EXPECT_EQ(link.ue.app_tx_count(), 3u);
    EXPECT_EQ(link.ue.app_rx_count(), 3u);
    EXPECT_GE(link.ue.last_app_rtt_ms(), 0);
}

TEST(E2eNodes, DetachReleasesBothEnds) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.ue.registered());
    uint16_t crnti = link.ue.crnti();
    ASSERT_TRUE(link.bs.ue_connected(crnti));

    link.ue.detach();

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
    ASSERT_TRUE(link.ue.registered());
    link.ue.detach();

    link.ue.attach(); // full second pass through RACH/RRC/NAS

    EXPECT_TRUE(link.ue.registered());
    EXPECT_NE(link.ue.crnti(), 0u);
    EXPECT_TRUE(link.bs.ue_connected(link.ue.crnti()));

    link.ue.send_app_data({0xAA, 0xBB});
    EXPECT_EQ(link.ue.app_rx_count(), 1u);
}

TEST(E2eNodes, SustainedTrafficLoopbackNoLoss) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.ue.registered());

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
    ASSERT_TRUE(link.ue.registered());
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
    // pings sent during the blackout are lost forever (TM, no retransmit)
    EXPECT_EQ(link.ue.app_rx_count(), 90u);         // 20 pre + 70 post-restore
    EXPECT_EQ(link.ue.app_loss_count(), 40u);       // the blackout phase
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
