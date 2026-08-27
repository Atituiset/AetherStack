// M6.5 T6: cross-layer E2E over an in-memory air interface.
// UeNode <-> BsNode exchange real air frames (pack_air_bits/encode_frame),
// covering SIB gating, RACH 4-step, RRC setup, NAS attach, user-plane
// ping-pong, detach and attach-guard fault recovery.
#include "core/bs_node.h"
#include "core/ue_node.h"
#include "cn/amf.h"
#include "cn/upf.h"
#include "mac/mac_pdu.h"
#include "rrc/rrc_messages.h"
#include "nas/aka.h"
#include "pdcp/pdcp_entity.h"
#include <gtest/gtest.h>
#include <array>
#include <functional>
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
    // M16.1: the DL AM window is now 64 deep (bounded staleness) and the
    // transmitter refuses new SDUs when congestion fills it, so a handful
    // of post-restore echoes can be counted as loss instead of delivered —
    // the full-accounting equality below remains the strong invariant.
    EXPECT_GE(link.ue.app_rx_count(), 85u);         // clean + post-restore
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
    pump(1500); // flush the tail: 250 ms HARQ timeout (M16.1) rescues
                // slowly but surely instead of firing early under load

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

TEST(E2eNodes, UeToUeMessageAndVoiceCall) {
    // M16: two attached UEs exchange user-plane traffic through the BS.
    // ue1 texts ue2 (exact payload must survive the full stack), then runs
    // a short voice call: media flows ue1 -> BS -> ue2, acks flow back.
    core::BsNode bs;
    core::UeNodeConfig c1; c1.imsi = "460011234567890";
    core::UeNodeConfig c2; c2.imsi = "460011234567891";
    core::UeNode ue1(c1), ue2(c2);

    ue1.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    ue2.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) {
        ue1.on_air_bits(b);
        ue2.on_air_bits(b);
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
    auto pump_until = [&](auto pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    };

    pump(10);
    bs.start_broadcast();
    ue1.attach();
    ASSERT_TRUE(pump_until([&] { return ue1.registered(); }, 400));
    ue2.attach();
    ASSERT_TRUE(pump_until([&] { return ue2.registered(); }, 400));

    // --- one-shot text message -------------------------------------------
    ue1.send_msg("460011234567891", "hello-from-ue1");
    ASSERT_TRUE(pump_until([&] { return ue2.msg_rx_count() == 1; }, 400));
    EXPECT_EQ(ue2.last_msg_src(), "460011234567890");
    EXPECT_EQ(ue2.last_msg_text(), "hello-from-ue1");
    EXPECT_EQ(ue1.msg_rx_count(), 0u);          // no crosstalk/echo back
    EXPECT_EQ(ue2.app_rx_count(), 0u);          // not a legacy pong

    // --- short voice call: full SIP-lite dialog (M17) ----------------------
    ue2.set_autoanswer(0); // deterministic: answer explicitly in tests
    ue1.start_call(app::MediaKind::VOICE, "460011234567891");   // INVITE
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 2; }, 400));
    EXPECT_EQ(ue2.call_peer(), "460011234567890"); // ringing, no media yet
    EXPECT_EQ(ue1.call_state(), 1);
    EXPECT_EQ(ue2.stream_rx_count(), 0u);
    ue2.answer();                                            // 200 OK
    ASSERT_TRUE(pump_until([&] { return ue1.call_state() == 3; }, 400));
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 3; }, 400));
    ASSERT_TRUE(pump_until([&] { return ue2.incoming_call_active(); }, 400));
    EXPECT_EQ(ue2.incoming_peer(), "460011234567890");
    pump(2000); // ~66 media packets at 30 ms intervals
    EXPECT_GT(ue1.stream_tx_count(), 50u);
    EXPECT_GT(ue2.stream_rx_count(), 50u);
    EXPECT_EQ(ue2.stream_loss_count(), 0u);     // clean in-memory channel
    EXPECT_GT(ue1.ack_rx_count(), 50u);         // acks made it back
    EXPECT_GE(ue1.stream_rtt_avg_ms(), 0);

    // Hang up: BYE both ways, ue2 sees the peer-end and the dialog clears.
    ue1.end_call();
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 0; }, 400));
    EXPECT_FALSE(ue1.call_active());
    EXPECT_FALSE(ue2.incoming_call_active());

    // Unknown destination falls back to the legacy echo behaviour.
    ue1.send_msg("460010000000000", "no-such-ue");
    pump(100);
    EXPECT_EQ(ue2.msg_rx_count(), 1u);          // not delivered to ue2
    EXPECT_EQ(ue1.msg_rx_count(), 0u);          // own echo is not consumed

    // Loopback ping-pong still works alongside the U2U machinery.
    const uint32_t rx_before = ue1.app_rx_count();
    ue1.send_app_data({'P'});
    pump(100);
    EXPECT_EQ(ue1.app_rx_count(), rx_before + 1);

    ue1.detach();
    ue2.detach();
    pump(40);
}

TEST(E2eNodes, UeToUeVoiceBlackoutRecoversAndLoopbackSurvives) {
    // M16.1 regression: an uplink blackout far longer than the AM TX window
    // (64 PDUs = ~1.3 s of voice) must not permanently wedge the bearer.
    // Pre-fix behaviour: the sender shed the oldest unacked PDUs, the BS
    // reassembly waited for them forever, and BOTH the media path and the
    // legacy loopback (same AM bearer) stopped delivering for good.
    core::BsNode bs;
    core::UeNodeConfig c1; c1.imsi = "460011234567890";
    core::UeNodeConfig c2; c2.imsi = "460011234567891";
    core::UeNode ue1(c1), ue2(c2);

    bool blackout = false;
    ue1.set_air_send([&](const std::vector<uint8_t>& b) {
        if (!blackout) bs.on_air_bits(b);
    });
    ue2.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) {
        ue1.on_air_bits(b);
        ue2.on_air_bits(b);
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
    auto pump_until = [&](auto pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    };

    pump(10);
    bs.start_broadcast();
    ue1.attach();
    ASSERT_TRUE(pump_until([&] { return ue1.registered(); }, 400));
    ue2.attach();
    ASSERT_TRUE(pump_until([&] { return ue2.registered(); }, 400));

    // Voice call runs cleanly (SIP dialog), then ue1's uplink blacks out
    // for 12 s.
    ue2.set_autoanswer(0);
    ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 2; }, 400));
    ue2.answer();
    ASSERT_TRUE(pump_until([&] { return ue2.stream_rx_count() > 10; }, 1000));
    const uint32_t rx_before = ue2.stream_rx_count();
    blackout = true;
    pump(12000); // window fills + shedding wedge under the pre-fix code
    blackout = false;

    // The bearer must heal: media delivery resumes towards ue2 after a
    // short (window-bounded) backlog; the packets refused while the window
    // was full surface as a seq gap once the backlog drains.
    ASSERT_TRUE(pump_until(
        [&] { return ue2.stream_rx_count() > rx_before + 50; }, 8000));
    // Graceful degradation: the blackout-era loss is accounted (seq gap),
    // not silently hidden — and delivery catches up to live quickly.
    ASSERT_TRUE(pump_until([&] { return ue2.stream_loss_count() > 0; },
                           8000));

    // Hang up, then prove the legacy loopback on the same bearer recovers.
    ue1.end_call();
    ASSERT_TRUE(pump_until([&] { return !ue2.incoming_call_active(); }, 3000));
    const uint32_t pong_before = ue1.app_rx_count();
    ue1.send_app_data({'P'});
    ASSERT_TRUE(pump_until(
        [&] { return ue1.app_rx_count() > pong_before; }, 1000));

    ue1.detach();
    ue2.detach();
    pump(40);
}

// ---- M17: SIP-lite call control ----------------------------------------------

namespace {

// Shared 3-UE/1-BS in-memory cell for the SIP dialog tests.
struct SipCell {
    core::BsNode bs;
    core::UeNode ue1, ue2, ue3;
    uint32_t clock = 1000;

    static core::UeNodeConfig cfg(const char* imsi, uint32_t preamble) {
        core::UeNodeConfig c;
        c.imsi = imsi;
        c.rach.preamble_index = static_cast<mac::PreambleIndex>(preamble);
        return c;
    }

    SipCell()
        : ue1(cfg("460011234567890", 42)),
          ue2(cfg("460011234567891", 43)),
          ue3(cfg("460011234567892", 44)) {
        ue1.set_air_send([this](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
        ue2.set_air_send([this](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
        ue3.set_air_send([this](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
        bs.set_air_send([this](const std::vector<uint8_t>& b) {
            ue1.on_air_bits(b);
            ue2.on_air_bits(b);
            ue3.on_air_bits(b);
        });
    }

    void pump(uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue1.tick(clock);
            ue2.tick(clock);
            ue3.tick(clock);
            bs.tick(clock);
        }
    }
    bool pump_until(std::function<bool()> pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    }

    void attach_all() {
        pump(10);
        bs.start_broadcast();
        ue1.set_autoanswer(0);
        ue2.set_autoanswer(0);
        ue3.set_autoanswer(0);
        ue1.attach();
        pump_until([this] { return ue1.registered(); }, 400);
        ue2.attach();
        pump_until([this] { return ue2.registered(); }, 400);
        ue3.attach();
        pump_until([this] { return ue3.registered(); }, 400);
    }

    // Drive a full INVITE->180->200->ACK dialog; ue1 ends up established
    // with media flowing towards ue2.
    void establish_ue1_calls_ue2() {
        ue1.start_call(app::MediaKind::VOICE, "460011234567891");
        pump_until([this] { return ue2.call_state() == 2; }, 400);
        ue2.answer();
        pump_until([this] { return ue1.call_state() == 3 &&
                                    ue2.call_state() == 3; }, 400);
    }
};

} // namespace

TEST(E2eNodes, SipDeclineProducesNoMedia) {
    // Callee declines (603): caller fails with reason=declined and not a
    // single media packet is generated on either side.
    SipCell l;
    l.attach_all();

    l.ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 2; }, 400));
    l.ue2.decline();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue1.call_state() == 0; }, 400));
    EXPECT_EQ(l.ue1.last_call_fail_reason(), "declined");
    l.pump(1000);
    EXPECT_EQ(l.ue1.stream_tx_count(), 0u);
    EXPECT_EQ(l.ue2.stream_rx_count(), 0u);
    EXPECT_EQ(l.ue2.call_state(), 0);
}

TEST(E2eNodes, SipBusyWhenCalleeOccupiedByThirdUe) {
    // ue1<->ue2 established; ue3's INVITE to ue2 gets 486 -> ue3 hears busy,
    // the original call is undisturbed.
    SipCell l;
    l.attach_all();
    l.establish_ue1_calls_ue2();
    ASSERT_EQ(l.ue1.call_state(), 3);

    l.ue3.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue3.call_state() == 0; }, 400));
    EXPECT_EQ(l.ue3.last_call_fail_reason(), "busy");
    EXPECT_EQ(l.ue1.call_state(), 3); // original dialog untouched
    EXPECT_EQ(l.ue2.call_state(), 3);
    EXPECT_EQ(l.ue2.call_peer(), "460011234567890");
    l.pump(500);
    EXPECT_EQ(l.ue3.stream_tx_count(), 0u); // busy caller sends no media

    l.ue1.end_call();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; }, 400));
}

TEST(E2eNodes, SipCallerCancelWhileRinging) {
    // Caller hangs up before the callee answers: CANCEL -> both sides idle,
    // no failure reason on the caller (local action), no media.
    SipCell l;
    l.attach_all();

    l.ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 2; }, 400));
    l.ue1.end_call(); // CANCEL while ringing
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; }, 400));
    EXPECT_EQ(l.ue1.call_state(), 0);
    EXPECT_EQ(l.ue1.stream_tx_count(), 0u);
}

TEST(E2eNodes, SipRingTimeoutFiresWhenCalleeSilent) {
    // Full no-answer timeout with a shortened ring_timeout_ms: the caller
    // CANCELS and fails with reason=timeout, the callee goes idle.
    core::BsNode bs;
    core::UeNodeConfig c1; c1.imsi = "460011234567890"; c1.ring_timeout_ms = 2000;
    core::UeNodeConfig c2; c2.imsi = "460011234567891";
    core::UeNode ue1(c1), ue2(c2);
    ue1.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    ue2.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) {
        ue1.on_air_bits(b);
        ue2.on_air_bits(b);
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
    auto pump_until = [&](auto pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    };
    pump(10);
    bs.start_broadcast();
    ue2.set_autoanswer(0); // never picks up
    ue1.attach();
    ASSERT_TRUE(pump_until([&] { return ue1.registered(); }, 400));
    ue2.attach();
    ASSERT_TRUE(pump_until([&] { return ue2.registered(); }, 400));

    ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 2; }, 400));
    // 2 s ring timeout -> CANCEL -> both sides idle, caller reason=timeout.
    ASSERT_TRUE(pump_until([&] { return ue1.call_state() == 0; }, 4000));
    EXPECT_EQ(ue1.last_call_fail_reason(), "timeout");
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 0; }, 1000));
    EXPECT_EQ(ue1.stream_tx_count(), 0u);
}

TEST(E2eNodes, SipUnreachableCalleeFailsWithReason) {
    // INVITE to an IMSI that is not registered: the BS echo-fallback means
    // no 180 ever comes back -> caller fails with reason=unreachable.
    SipCell l;
    l.attach_all();

    l.ue1.start_call(app::MediaKind::VOICE, "460010000000000");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue1.call_state() == 0; }, 8000));
    EXPECT_EQ(l.ue1.last_call_fail_reason(), "unreachable");
    EXPECT_EQ(l.ue1.stream_tx_count(), 0u);
}

TEST(E2eNodes, SipCalleeEstablishesFromMediaWhenAckLost) {
    // The ACK is control riding the same AM bearer as media: under
    // congestion an RLC reorder skip can sacrifice it (observed live). The
    // callee must then derive SIP_CALL_ESTABLISHED from the media itself
    // (media is ACK-gated on the caller, so it proves establishment).
    SipCell l;
    l.attach_all();

    l.ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 2; }, 400));

    // Drop ONLY sig-sized uplink bursts from ue1 (sig SDU ~48 B -> ~1 kbit
    // bursts; media SDU ~202 B -> ~3.5 kbit bursts). The ACK and all its AM
    // retransmissions die while media flows; the reorder skip then declares
    // the ACK's SN lost — exactly the live failure mode.
    bool drop_small = false;
    l.ue1.set_air_send([&](const std::vector<uint8_t>& b) {
        if (drop_small && b.size() < 2000) return;
        l.bs.on_air_bits(b);
    });
    drop_small = true;
    l.ue2.answer();
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue2.stream_rx_count() > 5 &&
                      l.ue2.call_established_logged(); },
        4000));
    EXPECT_EQ(l.ue2.call_state(), 3);
    EXPECT_TRUE(l.ue1.call_established_logged()); // via 200 OK (caller side)

    // Hang up still works (BYE) once signalling is let through again.
    drop_small = false;
    l.ue1.end_call();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; }, 1000));
}

TEST(E2eNodes, SipDetachMidCallNotifiesPeer) {
    // Detach while established: the peer sees BYE (best-effort) and tears
    // its side down.
    SipCell l;
    l.attach_all();
    l.establish_ue1_calls_ue2();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.stream_rx_count() > 0; }, 1000));

    l.ue1.detach();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; }, 1000));
    EXPECT_FALSE(l.ue2.incoming_call_active());
    l.pump(100);
    l.ue2.detach();
    l.ue3.detach();
    l.pump(40);
}

TEST(E2eNodes, QosBearerSetPriorityAndMinShare) {
    // M17: strict priority ctrl > sig > voice > video > best-effort, with
    // the BE min-share guard kicking in every 4th pick under full load.
    core::BearerSet bs;
    auto push = [&](core::Qci q, int n) {
        for (int i = 0; i < n; ++i) {
            bs.queue_of(q).push_back(
                {core::lcid_of(q), false, {static_cast<uint8_t>(q)}});
        }
    };
    push(core::Qci::BEST_EFFORT, 1);
    push(core::Qci::VIDEO, 1);
    push(core::Qci::VOICE, 1);
    push(core::Qci::SIG, 1);
    bs.ctrl().push_back({mac::LCID_NAS_DCCH, false, {0xCC}});

    // Drain: control first, then strict priority.
    std::vector<int> order;
    while (!bs.empty()) order.push_back(bs.pop_next().bytes[0]);
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order[0], 0xCC);                                // ctrl
    EXPECT_EQ(order[1], static_cast<int>(core::Qci::SIG));    // 5
    EXPECT_EQ(order[2], static_cast<int>(core::Qci::VOICE));  // 1
    EXPECT_EQ(order[3], static_cast<int>(core::Qci::VIDEO));  // 2
    EXPECT_EQ(order[4], static_cast<int>(core::Qci::BEST_EFFORT)); // 9

    // Full load on every class: BE must get at least every 4th pick
    // (min-share floor), never starve.
    core::BearerSet bs2;
    auto push2fn = [&](core::Qci q, int n) {
        for (int i = 0; i < n; ++i) {
            bs2.queue_of(q).push_back(
                {core::lcid_of(q), false, {static_cast<uint8_t>(q)}});
        }
    };
    push2fn(core::Qci::SIG, 4);
    push2fn(core::Qci::VOICE, 4);
    push2fn(core::Qci::VIDEO, 4);
    push2fn(core::Qci::BEST_EFFORT, 4);
    int be_count = 0;
    for (int i = 0; i < 16; ++i) {
        if (bs2.pop_next().bytes[0] ==
            static_cast<uint8_t>(core::Qci::BEST_EFFORT)) {
            ++be_count;
        }
    }
    EXPECT_EQ(be_count, 4); // 25% floor: 4 of 16 picks
}

TEST(E2eNodes, QosBearerSetupAndTeardownEvents) {
    // M17: a voice call sets up sig+voice bearers on both UEs and on the
    // BS flow; hangup tears them down on the UEs (the BS keeps its bearers
    // for the flow's lifetime — documented simplification).
    SipCell l;
    l.attach_all();

    EXPECT_FALSE(l.ue1.bearer_established(5));
    EXPECT_FALSE(l.ue1.bearer_established(1));
    l.establish_ue1_calls_ue2();
    // Voice bearers are set up lazily on the first media SDU — wait for it.
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.stream_rx_count() > 0; },
                             1000));
    EXPECT_TRUE(l.ue1.bearer_established(5));  // sig
    EXPECT_TRUE(l.ue1.bearer_established(1));  // voice
    EXPECT_TRUE(l.ue2.bearer_established(5));
    EXPECT_TRUE(l.ue2.bearer_established(1));
    EXPECT_TRUE(l.bs.flow_bearer_established(l.ue1.crnti(), 5));
    EXPECT_TRUE(l.bs.flow_bearer_established(l.ue1.crnti(), 1));
    EXPECT_TRUE(l.bs.flow_bearer_established(l.ue2.crnti(), 1));
    EXPECT_FALSE(l.ue1.bearer_established(2)); // no video bearer

    l.ue1.end_call();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; }, 1000));
    EXPECT_FALSE(l.ue1.bearer_established(5));
    EXPECT_FALSE(l.ue1.bearer_established(1));
    EXPECT_FALSE(l.ue2.bearer_established(5));
    // BS bearers live until the flow is erased (detach below).
    EXPECT_TRUE(l.bs.flow_bearer_established(l.ue1.crnti(), 1));

    const uint16_t r2 = l.ue2.crnti();
    l.ue1.detach();
    l.ue2.detach();
    l.pump(100);
    EXPECT_FALSE(l.bs.flow_bearer_established(r2, 1)); // flow erased
}

TEST(E2eNodes, QosConcurrentVoiceAndVideoCallsDifferentiate) {
    // M17 demo story: ue1 holds a video call to ue2 AND a voice call to
    // ue3 at the same time (multi-dialog). Both media flows run on their
    // own bearers; the best-effort loopback is not starved; kind-specific
    // hangup ends exactly one dialog.
    SipCell l;
    l.attach_all();

    // Video ue1 -> ue2.
    l.ue1.start_call(app::MediaKind::VIDEO, "460011234567891");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 2; }, 400));
    l.ue2.answer();
    // Voice ue1 -> ue3 while the video dialog is active (multi-dialog).
    l.ue1.start_call(app::MediaKind::VOICE, "460011234567892");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue3.call_state() == 2; }, 400));
    l.ue3.answer();
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue2.stream_rx_count() > 5 &&
                      l.ue3.stream_rx_count() > 5; },
        2000));
    EXPECT_EQ(l.ue1.call_state(), 3);
    EXPECT_TRUE(l.ue1.bearer_established(1)); // voice bearer
    EXPECT_TRUE(l.ue1.bearer_established(2)); // video bearer
    EXPECT_TRUE(l.ue2.bearer_established(2));
    EXPECT_TRUE(l.ue3.bearer_established(1));
    // A second dialog of the SAME kind is refused (one media source/class).
    l.ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    EXPECT_EQ(l.ue1.last_call_fail_reason(), "busy");
    l.pump(1500);
    EXPECT_GT(l.ue2.stream_rx_count(), 10u); // video keeps flowing
    EXPECT_GT(l.ue3.stream_rx_count(), 10u); // voice keeps flowing

    // Starvation guard: the best-effort loopback still gets answered while
    // two media bearers are busy.
    const uint32_t pong_before = l.ue1.app_rx_count();
    l.ue1.send_app_data({'P'});
    ASSERT_TRUE(l.pump_until(
        [&] { return l.ue1.app_rx_count() > pong_before; }, 1500));

    // Kind-specific hangup: voice ends, video survives; then video ends.
    l.ue1.end_call(app::MediaKind::VOICE);
    ASSERT_TRUE(l.pump_until([&l] { return l.ue3.call_state() == 0; }, 1000));
    EXPECT_EQ(l.ue2.call_state(), 3); // video dialog untouched
    EXPECT_FALSE(l.ue1.bearer_established(1)); // voice bearer released
    EXPECT_TRUE(l.ue1.bearer_established(2));
    l.ue1.end_call(app::MediaKind::VIDEO);
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; }, 1000));
    EXPECT_EQ(l.ue1.call_state(), 0);
}

TEST(E2eNodes, QosVoiceProtectedWhenVideoSaturatesPipe) {
    // M17 priority under contention: every air hop is delayed 70 ms, so a
    // HARQ round costs ~150 ms and the 8-process uplink pipe carries only
    // ~53 blocks/s. The video flood alone offers ~60 blocks/s (512 B SDU =
    // 3 UM segments every 50 ms); adding a voice call (~33 blocks/s, one
    // segment per 30 ms packet) overbooks the pipe ~1.8x. Strict-priority
    // bearer scheduling must keep voice RTT bounded and lossless while the
    // video bearer absorbs the congestion (queue-cap shedding, high RTT),
    // and the best-effort min-share guard must still answer a loopback
    // ping in the middle of the flood.
    core::BsNode bs;
    auto cfg = [](const char* imsi, uint32_t preamble) {
        core::UeNodeConfig c;
        c.imsi = imsi;
        c.rach.preamble_index = static_cast<mac::PreambleIndex>(preamble);
        return c;
    };
    core::UeNode ue1(cfg("460011234567890", 42));
    core::UeNode ue2(cfg("460011234567891", 43));
    core::UeNode ue3(cfg("460011234567892", 44));

    constexpr uint32_t kAirDelayMs = 70;
    uint32_t clock = 1000;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> ul, dl;
    auto delay_ul = [&](const std::vector<uint8_t>& b) {
        ul.emplace_back(clock + kAirDelayMs, b);
    };
    ue1.set_air_send(delay_ul);
    ue2.set_air_send(delay_ul);
    ue3.set_air_send(delay_ul);
    bs.set_air_send([&](const std::vector<uint8_t>& b) {
        dl.emplace_back(clock + kAirDelayMs, b);
    });

    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            while (!ul.empty() && ul.front().first <= clock) {
                bs.on_air_bits(ul.front().second);
                ul.pop_front();
            }
            while (!dl.empty() && dl.front().first <= clock) {
                ue1.on_air_bits(dl.front().second);
                ue2.on_air_bits(dl.front().second);
                ue3.on_air_bits(dl.front().second);
                dl.pop_front();
            }
            ue1.tick(clock);
            ue2.tick(clock);
            ue3.tick(clock);
            bs.tick(clock);
        }
    };
    auto pump_until = [&](auto pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    };

    pump(10);
    bs.start_broadcast();
    ue1.set_autoanswer(0);
    ue2.set_autoanswer(0);
    ue3.set_autoanswer(0);
    ue1.attach();
    ASSERT_TRUE(pump_until([&] { return ue1.registered(); }, 6000));
    ue2.attach();
    ASSERT_TRUE(pump_until([&] { return ue2.registered(); }, 6000));
    ue3.attach();
    ASSERT_TRUE(pump_until([&] { return ue3.registered(); }, 6000));

    // Video flood ue1 -> ue2 first, then voice ue1 -> ue3 on top of it.
    ue1.start_call(app::MediaKind::VIDEO, "460011234567891");
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 2; }, 4000));
    ue2.answer();
    ASSERT_TRUE(pump_until([&] { return ue2.stream_rx_count() > 5; }, 4000));
    ue1.start_call(app::MediaKind::VOICE, "460011234567892");
    ASSERT_TRUE(pump_until([&] { return ue3.call_state() == 2; }, 4000));
    ue3.answer();
    ASSERT_TRUE(pump_until([&] { return ue3.stream_rx_count() > 5; }, 4000));

    // Let the flood run: the video queue hits its cap and starts shedding.
    pump(8000);

    // Voice is protected: keeps flowing, bounded RTT, no loss.
    EXPECT_GT(ue3.stream_rx_count(app::MediaKind::VOICE), 150u);
    const int64_t voice_rtt = ue1.stream_rtt_avg_ms(app::MediaKind::VOICE);
    ASSERT_GE(voice_rtt, 0);
    EXPECT_LT(voice_rtt, 800);
    EXPECT_EQ(ue1.stream_loss_count(app::MediaKind::VOICE), 0u);
    EXPECT_EQ(ue3.stream_loss_count(app::MediaKind::VOICE), 0u);

    // Video absorbs the congestion: markedly worse RTT and real loss.
    const int64_t video_rtt = ue1.stream_rtt_avg_ms(app::MediaKind::VIDEO);
    ASSERT_GE(video_rtt, 0);
    EXPECT_GT(video_rtt, 2 * voice_rtt);
    EXPECT_GT(ue1.stream_loss_count(app::MediaKind::VIDEO) +
                  ue2.stream_loss_count(app::MediaKind::VIDEO),
              0u);

    // Starvation guard: a best-effort loopback ping is still answered.
    const uint32_t pong_before = ue1.app_rx_count();
    ue1.send_app_data({'P'});
    ASSERT_TRUE(pump_until(
        [&] { return ue1.app_rx_count() > pong_before; }, 3000));

    ue1.end_call(app::MediaKind::VOICE);
    ue1.end_call(app::MediaKind::VIDEO);
    ASSERT_TRUE(pump_until([&] { return ue1.call_state() == 0; }, 4000));
    ue1.detach();
    ue2.detach();
    ue3.detach();
    pump(200);
}

TEST(E2eNodes, ConfThreePartyBridgeFlow) {
    // M18 demo story: ue1 hosts a conference with ue2 and ue3. Both ring
    // and join; the BS bridge fans every participant's voice out to the
    // other two (each party receives from BOTH others); ue3 leaves and the
    // remaining two continue; the host's conf-end tears everything down.
    SipCell l;
    l.attach_all();

    l.ue1.start_conf("460011234567891", "460011234567892");
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue2.call_state() == 2 && l.ue3.call_state() == 2; },
        400));
    l.ue2.answer();
    l.ue3.answer();
    const uint32_t conf_id = l.ue1.active_conf_id();
    ASSERT_NE(conf_id, 0u);

    // Full mesh through the bridge: every party hears BOTH others.
    ASSERT_TRUE(l.pump_until(
        [&] {
            return l.ue1.stream_rx_from("460011234567891") > 3 &&
                   l.ue1.stream_rx_from("460011234567892") > 3 &&
                   l.ue2.stream_rx_from("460011234567890") > 3 &&
                   l.ue2.stream_rx_from("460011234567892") > 3 &&
                   l.ue3.stream_rx_from("460011234567890") > 3 &&
                   l.ue3.stream_rx_from("460011234567891") > 3;
        },
        2000));
    EXPECT_EQ(l.bs.conf_member_count(conf_id), 3u);

    // ue3 leaves (participant "call end"): the conference continues.
    l.ue3.end_call();
    ASSERT_TRUE(l.pump_until(
        [&] { return l.bs.conf_member_count(conf_id) == 2; }, 400));
    EXPECT_EQ(l.ue3.call_state(), 0);
    const uint32_t rx1 = l.ue1.stream_rx_from("460011234567891");
    const uint32_t rx2 = l.ue2.stream_rx_from("460011234567890");
    l.pump(500);
    EXPECT_GT(l.ue1.stream_rx_from("460011234567891"), rx1);
    EXPECT_GT(l.ue2.stream_rx_from("460011234567890"), rx2);

    // Host ends the conference: both dialogs BYEd, parties torn down.
    l.ue1.end_conf();
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue1.call_state() == 0 && l.ue2.call_state() == 0; },
        1000));
    EXPECT_EQ(l.ue1.active_conf_id(), 0u);
    EXPECT_EQ(l.bs.conf_count(), 0u);
}

TEST(E2eNodes, ConfDeclineKeepsTwoPartyConference) {
    // M18: one invited party declines — the conference proceeds with the
    // two remaining parties instead of aborting.
    SipCell l;
    l.attach_all();

    l.ue1.start_conf("460011234567891", "460011234567892");
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue2.call_state() == 2 && l.ue3.call_state() == 2; },
        400));
    const uint32_t conf_id = l.ue1.active_conf_id();
    l.ue2.answer();
    l.ue3.decline();

    ASSERT_TRUE(l.pump_until(
        [&] {
            return l.ue1.stream_rx_from("460011234567891") > 3 &&
                   l.ue2.stream_rx_from("460011234567890") > 3;
        },
        2000));
    EXPECT_EQ(l.bs.conf_member_count(conf_id), 2u);
    EXPECT_EQ(l.ue3.call_state(), 0);
    EXPECT_EQ(l.ue3.stream_tx_count(), 0u); // declining party never talks

    l.ue1.end_conf();
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue1.call_state() == 0 && l.ue2.call_state() == 0; },
        1000));
    EXPECT_EQ(l.bs.conf_count(), 0u);
}

TEST(E2eNodes, ConfInviteToBusyUeGets486) {
    // M18 interop: conference INVITEs to UEs occupied in a 1:1 call get 486
    // like any call; with every invitation refused the BS closes the empty
    // conference and the original call is undisturbed.
    SipCell l;
    l.attach_all();

    // ue2 <-> ue3 in an established 1:1 voice call.
    l.ue2.start_call(app::MediaKind::VOICE, "460011234567892");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue3.call_state() == 2; }, 400));
    l.ue3.answer();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 3; }, 400));

    l.ue1.start_conf("460011234567891", "460011234567892");
    // Both 486s must come back: ue1's conf dialogs fail "busy"...
    ASSERT_TRUE(l.pump_until(
        [&l] {
            return l.ue1.call_state() == 0 &&
                   l.ue1.last_call_fail_reason() == "busy";
        },
        800));
    // ...and with every invitation refused the BS closes the conference.
    ASSERT_TRUE(l.pump_until([&] { return l.bs.conf_count() == 0; }, 400));
    EXPECT_EQ(l.ue1.stream_tx_count(), 0u); // no conf media ever sent
    EXPECT_EQ(l.ue2.call_state(), 3);       // original call undisturbed
    EXPECT_EQ(l.ue3.call_state(), 3);

    l.ue2.end_call();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue3.call_state() == 0; }, 400));
}

// ---- M20: RRC_INACTIVE + fast resume -----------------------------------------

namespace {

// 2-UE cell with a short inactivity timer for suspend/resume tests.
// air_delay_ms > 0 routes every hop through a delay queue (used by the
// resume-vs-attach cost test to turn signalling hop counts into time).
struct InactiveCell {
    core::BsNode bs;
    core::UeNode ue1, ue2;
    uint32_t clock = 1000;
    uint32_t air_delay_ms = 0;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> ul_q, dl_q;

    static core::UeNodeConfig uecfg(const char* imsi, uint32_t preamble) {
        core::UeNodeConfig c;
        c.imsi = imsi;
        c.rach.preamble_index = static_cast<mac::PreambleIndex>(preamble);
        return c;
    }

    explicit InactiveCell(uint32_t inactive_ms = 2000)
        : bs([inactive_ms] {
              core::BsNodeConfig c;
              c.inactive_ms = inactive_ms;
              return c;
          }()),
          ue1(uecfg("460011234567890", 42)),
          ue2(uecfg("460011234567891", 43)) {
        ue1.set_air_send([this](const std::vector<uint8_t>& b) { ul_send(b); });
        ue2.set_air_send([this](const std::vector<uint8_t>& b) { ul_send(b); });
        bs.set_air_send([this](const std::vector<uint8_t>& b) { dl_send(b); });
    }

    void ul_send(const std::vector<uint8_t>& b) {
        if (air_delay_ms == 0) bs.on_air_bits(b);
        else ul_q.emplace_back(clock + air_delay_ms, b);
    }
    void dl_send(const std::vector<uint8_t>& b) {
        if (air_delay_ms == 0) {
            ue1.on_air_bits(b);
            ue2.on_air_bits(b);
        } else {
            dl_q.emplace_back(clock + air_delay_ms, b);
        }
    }

    void pump(uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            while (!ul_q.empty() && ul_q.front().first <= clock) {
                bs.on_air_bits(ul_q.front().second);
                ul_q.pop_front();
            }
            while (!dl_q.empty() && dl_q.front().first <= clock) {
                ue1.on_air_bits(dl_q.front().second);
                ue2.on_air_bits(dl_q.front().second);
                dl_q.pop_front();
            }
            ue1.tick(clock);
            ue2.tick(clock);
            bs.tick(clock);
        }
    }
    bool pump_until(std::function<bool()> pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    }

    void attach_both() {
        pump(10);
        bs.start_broadcast();
        ue1.set_autoanswer(0);
        ue2.set_autoanswer(0);
        ue1.attach();
        pump_until([this] { return ue1.registered(); }, 6000);
        ue2.attach();
        pump_until([this] { return ue2.registered(); }, 6000);
    }

    void suspend_ue1(uint32_t bound_ms = 4000) {
        ASSERT_TRUE(pump_until([this] { return ue1.inactive(); }, bound_ms));
    }
};

} // namespace

TEST(E2eNodes, RrcInactiveSuspendResumePreservesSession) {
    // M20 demo story: screen off (suspend) -> screen on (activity) — the
    // UE resumes with registration, bearers and keys intact, and media
    // works right after.
    InactiveCell l;
    l.attach_both();
    const uint16_t r1 = l.ue1.crnti();

    // Inactivity timer suspends ue1; its BS flow parks, RRC state is
    // INACTIVE on both sides, NAS registration is kept.
    l.suspend_ue1();
    EXPECT_EQ(l.ue1.rrc_state(), rrc::UeState::INACTIVE);
    EXPECT_TRUE(l.bs.flow_suspended(r1));
    EXPECT_TRUE(l.ue1.registered()); // registration retained
    const uint16_t r2 = l.ue2.crnti();
    l.pump(2500); // ue2 also goes inactive (it is idle too)

    // Outbound activity: message queues, resumes, gets delivered.
    l.ue1.send_msg("460011234567891", "back-online");
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue2.msg_rx_count() > 0; }, 2000));
    EXPECT_EQ(l.ue2.last_msg_text(), "back-online");
    EXPECT_EQ(l.ue1.rrc_state(), rrc::UeState::CONNECTED);
    EXPECT_FALSE(l.bs.flow_suspended(l.ue1.crnti()));
    EXPECT_TRUE(l.ue1.registered());

    // Voice call right after the resume — bearers/keys were preserved.
    l.ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 2; },
                             2000));
    l.ue2.answer();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.stream_rx_count() > 5; },
                             2000));
    l.ue1.end_call();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.call_state() == 0; },
                             1000));
    (void)r2;
}

TEST(E2eNodes, RrcResumeIsCheaperThanAttach) {
    // M20: the resume shortcut must measurably beat a full attach. The air
    // is delayed 30 ms/hop so signalling hop counts surface as time:
    // attach = RACH(4) + RRC setup(3) + NAS attach/auth/accept(~6) hops,
    // resume = RACH(4) + RESUME_REQUEST/OK(2) hops.
    InactiveCell l(1500);
    l.air_delay_ms = 30;
    l.pump(10);
    l.bs.start_broadcast();
    l.ue1.set_autoanswer(0);
    l.ue2.set_autoanswer(0);

    const uint32_t t_attach0 = l.clock;
    l.ue1.attach();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue1.registered(); }, 4000));
    const uint32_t attach_ms = l.clock - t_attach0;

    l.ue2.attach();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue2.registered(); }, 4000));
    l.suspend_ue1(6000);

    const uint32_t t_wake0 = l.clock;
    l.ue1.wake();
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue1.rrc_state() == rrc::UeState::CONNECTED; },
        4000));
    const uint32_t resume_ms = l.clock - t_wake0;

    EXPECT_LT(resume_ms, attach_ms)
        << "resume " << resume_ms << " ms vs attach " << attach_ms << " ms";
}

TEST(E2eNodes, RrcResumeStaleIdFallsBackToFullSetup) {
    // M20: the network forgot the resume identity (context dropped out of
    // band) — RESUME_FAILURE and the UE re-attaches cleanly.
    InactiveCell l;
    l.attach_both();
    const uint16_t r1 = l.ue1.crnti();
    l.suspend_ue1();

    l.bs.rrc().release_context(r1); // context gone on the network side
    l.ue1.wake();
    // Registration is RETAINED through the suspend, so "registered" alone
    // is true immediately — wait for the full fallback to reconnect.
    ASSERT_TRUE(l.pump_until(
        [&l] {
            return l.ue1.registered() &&
                   l.ue1.rrc_state() == rrc::UeState::CONNECTED;
        },
        4000));
    // Full fallback: the loopback works end to end again.
    const uint32_t pong_before = l.ue1.app_rx_count();
    l.ue1.send_app_data({'P'});
    ASSERT_TRUE(l.pump_until(
        [&] { return l.ue1.app_rx_count() > pong_before; }, 1500));
}

TEST(E2eNodes, PagingWakesInactiveUeOnIncomingCall) {
    // M20: an incoming call to an INACTIVE UE pages it over the broadcast
    // channel; the UE resumes, the queued INVITE is delivered and the
    // dialog completes.
    InactiveCell l;
    l.attach_both();
    l.suspend_ue1();

    l.ue2.start_call(app::MediaKind::VOICE, "460011234567890");
    // BS pages ue1 -> resume -> queued INVITE delivered -> ue1 rings.
    ASSERT_TRUE(l.pump_until([&l] { return l.ue1.call_state() == 2; },
                             3000));
    EXPECT_EQ(l.ue1.rrc_state(), rrc::UeState::CONNECTED);
    l.ue1.answer();
    ASSERT_TRUE(l.pump_until(
        [&l] { return l.ue1.stream_rx_count() > 3; }, 2000));
    l.ue2.end_call();
    ASSERT_TRUE(l.pump_until([&l] { return l.ue1.call_state() == 0; },
                             1000));
}

TEST(E2eNodes, DetachFromInactiveWorks) {
    // M20: detach while INACTIVE resumes first, then tears the network
    // context down cleanly.
    InactiveCell l;
    l.attach_both();
    const uint16_t r1 = l.ue1.crnti();
    l.suspend_ue1();

    l.ue1.detach();
    ASSERT_TRUE(l.pump_until([&l] { return !l.ue1.registered(); }, 3000));
    EXPECT_EQ(l.bs.registered_ue_count(), 1u); // only ue2 remains
    EXPECT_EQ(l.ue1.rrc_state(), rrc::UeState::IDLE);
    (void)r1;
}

TEST(E2eNodes, LinkAdaptationFollowsChannelQuality) {
    // M19: the UE derives CQI from the DMRS SNR of decoded DL bursts and
    // reports it via MAC CE; the BS re-selects the downlink MCS per flow
    // (16qam >= cqi 14 | qpsk below — measured decode curve). Metrics are
    // injected at the radio edge here; live they come from phy_rx_frame.
    SipCell l;
    float snr_ue1 = 28.f; // good link
    l.bs.set_air_send([&](const std::vector<uint8_t>& b) {
        l.ue1.on_air_bits_with_metrics(b, snr_ue1, -18.f);
        l.ue2.on_air_bits(b);
        l.ue3.on_air_bits(b);
    });
    l.attach_all();
    const uint16_t r1 = l.ue1.crnti();
    EXPECT_EQ(l.bs.dl_mcs(r1), 0); // no CQI yet: robust QPSK

    ASSERT_TRUE(l.pump_until([&] { return l.bs.dl_mcs(r1) == 1; }, 5000));
    EXPECT_EQ(l.bs.flow_cqi(r1), 15); // snr 28 -> cqi 15 -> 16QAM

    snr_ue1 = 21.f; // mid link -> QPSK (16QAM needs ~26 dB measured)
    ASSERT_TRUE(l.pump_until([&] { return l.bs.dl_mcs(r1) == 0; }, 8000));

    snr_ue1 = 27.f; // back over the 16QAM floor (cqi >= 14)
    ASSERT_TRUE(l.pump_until([&] { return l.bs.dl_mcs(r1) == 1; }, 8000));

    l.ue1.detach();
    l.ue2.detach();
    l.ue3.detach();
    l.pump(200);
}

TEST(E2eNodes, TpcSteersUeTxPowerTowardsTargetSnr) {
    // M19: the BS measures UL arrival SNR per flow and orders +/-1 dB TPC;
    // the UE folds it into its TX power (open-loop pathloss base + TPC
    // accumulator). Weak UL -> power rises; strong UL -> power falls.
    SipCell l;
    float ul_snr = 5.f; // arrives far below the 15 dB target
    l.ue1.set_air_send([&](const std::vector<uint8_t>& b) {
        l.bs.on_air_bits_with_metrics(b, ul_snr, -18.f);
    });
    l.bs.set_air_send([&](const std::vector<uint8_t>& b) {
        l.ue1.on_air_bits_with_metrics(b, 20.f, -18.f);
        l.ue2.on_air_bits(b);
        l.ue3.on_air_bits(b);
    });
    l.attach_all();
    l.ue1.start_traffic(); // steady UL reference bursts

    // The closed loop climbs from wherever the open loop landed (the TPC
    // accumulator is already trimming by the first service tick).
    l.pump(1500);
    const double start = l.ue1.tx_power_db();
    l.pump(6000); // TPC +1 dB every 500 ms while SNR < target-2
    const double raised = l.ue1.tx_power_db();
    EXPECT_GT(raised, start + 3.0);
    EXPECT_GT(raised, 2.0); // weak UL pushed power well up

    ul_snr = 27.f; // now arriving too strong (target 23 dB)
    l.pump(6000);
    EXPECT_LT(l.ue1.tx_power_db(), raised - 2.0);

    l.ue1.detach();
    l.ue2.detach();
    l.ue3.detach();
    l.pump(200);
}

// ---- M21: 5G-AKA-style authentication ----------------------------------------

namespace {

// Direct NasUe <-> NasBs pair (no RRC/RACH): drives the AKA exchange
// synchronously through the send callbacks, with a pinned RAND source so
// "freshness" is deterministic.
struct AkaPair {
    static constexpr const char* kImsi = "460019999999999";
    nas::NasBs bs;
    nas::NasUe ue;
    std::array<uint8_t, crypto::kKey256Size> key;

    AkaPair() {
        key.fill(0xA5);
        bs.add_subscriber(kImsi, key);
        ue.set_usim_key(key);
        bs.set_send_callback(
            [this](uint32_t, const std::vector<uint8_t>& p) {
                ue.on_message(p);
            });
        ue.set_send_callback([this](const std::vector<uint8_t>& p) {
            bs.handle_message(0, p);
        });
        bs.set_rand_fn([n = 1]() mutable {
            std::array<uint8_t, nas::aka::kRandLen> r{};
            r[nas::aka::kRandLen - 1] = static_cast<uint8_t>(n++);
            return r;
        });
    }

    void attach() { ue.send_attach_request(kImsi); }
};

} // namespace

TEST(E2eNodes, AkaHappyPathFreshKeysPerAttach) {
    // M21: full AKA (RAND/AUTN -> RES -> success), KASME agreed on both
    // ends, and a re-attach derives DIFFERENT session keys (fresh RAND).
    AkaPair l;
    l.attach();
    ASSERT_EQ(l.ue.state(), nas::UeState::REGISTERED);
    ASSERT_TRUE(l.ue.authenticated());
    const uint32_t tmsi = l.ue.assigned_tmsi();
    ASSERT_NE(l.bs.session_key(tmsi), nullptr);
    const auto k1 = l.ue.session_key();
    EXPECT_EQ(k1, *l.bs.session_key(tmsi)); // KASME from CK||IK, both sides
    EXPECT_EQ(l.ue.usim_sqn(), 1u);

    l.ue.send_detach();
    l.attach();
    ASSERT_EQ(l.ue.state(), nas::UeState::REGISTERED);
    EXPECT_NE(l.ue.session_key(), k1); // fresh RAND -> fresh keys
    EXPECT_EQ(l.ue.usim_sqn(), 2u);
    EXPECT_EQ(l.bs.subscriber_sqn(AkaPair::kImsi), 2u);
}

TEST(E2eNodes, AkaMacFailureRejectsNetwork) {
    // M21: a network that cannot prove knowledge of K (wrong key on the
    // USIM side) fails the AUTN MAC check — the UE sends AUTH_FAILURE
    // (cause MAC failure) and never registers.
    AkaPair l;
    std::array<uint8_t, crypto::kKey256Size> bad;
    bad.fill(0x5E);
    l.ue.set_usim_key(bad);
    l.attach();
    EXPECT_NE(l.ue.state(), nas::UeState::REGISTERED);
    EXPECT_FALSE(l.ue.authenticated());
    EXPECT_EQ(l.ue.usim_sqn(), 0u); // no SQN was ever accepted
    ASSERT_EQ(l.bs.session_key(l.ue.assigned_tmsi()), nullptr);
}

TEST(E2eNodes, AkaStaleSqnTriggersAutsResync) {
    // M21: the network's SQN falls behind the USIM's (context reset out of
    // band) -> the UE answers with a synchronisation failure + AUTS, the
    // network resynchronises from it and the retry succeeds.
    AkaPair l;
    l.attach();
    ASSERT_EQ(l.ue.state(), nas::UeState::REGISTERED);
    l.ue.send_detach();

    l.bs.set_subscriber_sqn(AkaPair::kImsi, 0); // network went back in time
    l.attach(); // challenge SQN=1 <= USIM's 1 -> AUTS -> retry with SQN=2
    ASSERT_EQ(l.ue.state(), nas::UeState::REGISTERED);
    EXPECT_TRUE(l.ue.authenticated());
    EXPECT_EQ(l.ue.usim_sqn(), 2u);
    EXPECT_EQ(l.bs.subscriber_sqn(AkaPair::kImsi), 2u); // resynchronised
    EXPECT_EQ(l.ue.session_key(), *l.bs.session_key(l.ue.assigned_tmsi()));
}

TEST(E2eNodes, AkaResMismatchRejects) {
    // M21: a RES that does not match XRES (tampered in flight here) gets
    // the attach rejected — no registration, no session key.
    AkaPair l;
    l.ue.set_send_callback([&l](const std::vector<uint8_t>& p) {
        auto msg = nas::NasMessage::decode(p);
        if (msg.msg_type == nas::NasMessageType::AUTH_RESPONSE &&
            !msg.value.empty()) {
            msg.value[0] ^= 0xFF; // tamper with the response
            l.bs.handle_message(0, msg.encode());
            return;
        }
        l.bs.handle_message(0, p);
    });
    l.attach();
    EXPECT_NE(l.ue.state(), nas::UeState::REGISTERED);
    EXPECT_FALSE(l.ue.authenticated());
    ASSERT_EQ(l.bs.session_key(l.ue.assigned_tmsi()), nullptr);
}

TEST(E2eNodes, AkaDerivedKeyDrivesPdcpBothWays) {
    // M21: the KASME agreed during AKA is exactly what PDCP uses — a frame
    // protected with one side's key must unprotect with the other's, in
    // both directions (and fail with a foreign key).
    AkaPair l;
    l.attach();
    ASSERT_EQ(l.ue.state(), nas::UeState::REGISTERED);
    const auto ue_key = l.ue.session_key();
    const auto bs_key = *l.bs.session_key(l.ue.assigned_tmsi());

    const std::vector<uint8_t> sdu = {'s', 'e', 'c', 'r', 'e', 't'};
    std::vector<uint8_t> out;
    EXPECT_TRUE(pdcp::unprotect(bs_key, pdcp::protect(ue_key, 7, sdu), out));
    EXPECT_EQ(out, sdu); // UE -> BS direction
    out.clear();
    EXPECT_TRUE(pdcp::unprotect(ue_key, pdcp::protect(bs_key, 9, sdu), out));
    EXPECT_EQ(out, sdu); // BS -> UE direction
    std::array<uint8_t, crypto::kKey256Size> foreign{};
    foreign.fill(0x11);
    EXPECT_FALSE(pdcp::unprotect(foreign, pdcp::protect(ue_key, 1, sdu), out));
}

TEST(E2eNodes, AuthenticatedAttachWithEncryptedUserPlane) {
    // M12/M21: provision a subscriber (USIM key on the UE, same key in the
    // HSS). Attach must include the AKA exchange (RAND/AUTN -> RES), and
    // the user plane must flow with PDCP confidentiality keyed by the
    // derived KASME.
    core::BsNode bs;
    core::UeNodeConfig ue_cfg;
    ue_cfg.imsi = "460019999999999";
    core::UeNode ue(ue_cfg);

    std::array<uint8_t, crypto::kKey256Size> usim_key{};
    for (size_t i = 0; i < usim_key.size(); ++i) {
        usim_key[i] = static_cast<uint8_t>(0xA0 + i);
    }
    const std::string imsi = "460019999999999";
    ue.nas().set_usim_key(usim_key);
    bs.nas().add_subscriber(imsi, usim_key);

    ue.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs.tick(clock);
        }
    };
    pump(10);
    bs.start_broadcast();
    ue.attach();
    for (int i = 0; i < 40 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());
    EXPECT_TRUE(ue.nas().authenticated());

    // Encrypted loopback.
    for (int i = 0; i < 4; ++i) {
        ue.send_app_data({'S', 'E', 'C', static_cast<uint8_t>('0' + i)});
        pump(40);
    }
    EXPECT_EQ(ue.app_tx_count(), 4u);
    EXPECT_EQ(ue.app_rx_count(), 4u);
}

TEST(E2eNodes, WrongUsimKeyIsRejected) {
    // A UE presenting a key that does not match the HSS entry must fail
    // authentication and never reach REGISTERED. M21: with the AKA
    // exchange this now fails on the UE's AUTN MAC check (AUTH_FAILURE
    // cause "mac") instead of the old RES mismatch.

    core::BsNode bs;
    core::UeNodeConfig ue_cfg;
    ue_cfg.imsi = "460018888888888";
    ue_cfg.attach_guard_ms = 500; // short guard: the abort must fit the test
    core::UeNode ue(ue_cfg);

    std::array<uint8_t, crypto::kKey256Size> good{};
    std::array<uint8_t, crypto::kKey256Size> bad{};
    bad.fill(0x5E);
    const std::string imsi = "460018888888888";
    ue.nas().set_usim_key(bad);
    bs.nas().add_subscriber(imsi, good);

    ue.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs.tick(clock);
        }
    };
    pump(10);
    bs.start_broadcast();
    ue.attach();
    for (int i = 0; i < 40 && !ue.nas().authenticated(); ++i) pump(10);

    // The BS rejected the bogus response; the UE stays unauthenticated and
    // the guard timer eventually aborts the whole attach.
    EXPECT_FALSE(ue.nas().authenticated());
    for (int i = 0; i < 40 && ue.rrc_state() != rrc::UeState::IDLE; ++i) {
        pump(20);
    }
    EXPECT_EQ(ue.rrc_state(), rrc::UeState::IDLE);
}

// ---- M14: mobility ----------------------------------------------------------

namespace {

core::BsNode make_cell(uint16_t cell_id, uint16_t pci) {
    core::BsNodeConfig c;
    c.cell_id = cell_id;
    c.pci = pci;
    // M22: cell-scoped C-RNTI space (cell 2 starts at 16385 = 0x4001).
    c.crnti_base = cell_id == 1 ? 0x0001 : 0x4001;
    return core::BsNode(c);
}

struct TwoCellLink {
    core::BsNode bs_a; // cell 1
    core::BsNode bs_b; // cell 2
    core::UeNode ue;
    uint32_t clock = 1000;

    TwoCellLink()
        : bs_a(make_cell(1, 0)),
          bs_b(make_cell(2, 1)),
          ue([] {
              core::UeNodeConfig c;
              c.meas_period_ms = 100;
              return c;
          }()) {
        // Shared radio: UL fan-out to both cells, DL from both cells.
        ue.set_air_send([this](const std::vector<uint8_t>& bits) {
            bs_a.on_air_bits(bits);
            bs_b.on_air_bits(bits);
        });
        bs_a.set_air_send([this](const std::vector<uint8_t>& bits) {
            ue.on_air_bits(bits);
        });
        bs_b.set_air_send([this](const std::vector<uint8_t>& bits) {
            ue.on_air_bits(bits);
        });

        // X2-like preparation: cell 2 accepts contexts from anywhere.
        bs_a.set_ho_coordinator(
            [this](uint16_t target, const core::BsNode::HoContext& ctx)
                -> std::optional<uint16_t> {
                return target == 2 ? std::optional<uint16_t>(
                                         bs_b.prepare_handover(ctx))
                                   : std::nullopt;
            });
    }

    template <typename Pred>
    bool pump_until(Pred pred, uint32_t max_ms = 400) {
        for (uint32_t e = 0; e < max_ms; e += 10) {
            if (pred()) return true;
            pump(10);
        }
        return pred();
    }

    void pump(uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs_a.tick(clock);
            bs_b.tick(clock);
        }
    }
};

} // namespace

TEST(E2eNodes, HandoverBetweenCellsKeepsRegistrationAndSecurity) {
    TwoCellLink l;

    // Boot cell 1 alone so the UE camps on it, then light up cell 2.
    l.pump(10);
    l.bs_a.start_broadcast();
    l.ue.attach();
    ASSERT_TRUE(l.pump_until([&] { return l.ue.registered(); }));
    ASSERT_EQ(l.ue.serving_cell(), 1u);
    const uint32_t tmsi_before = l.ue.nas().assigned_tmsi();
    ASSERT_NE(tmsi_before, 0u);

    l.bs_b.start_broadcast();
    l.pump(300); // both cells audible now
    ASSERT_EQ(l.ue.serving_cell(), 1u);

    // Cell 1 beacon goes dark -> the next report lacks it -> handover.
    uint16_t old_crnti = l.ue.crnti();
    l.bs_a.set_sib_enabled(false);
    ASSERT_TRUE(l.pump_until([&] { return l.ue.serving_cell() == 2u; }, 1500));

    EXPECT_NE(l.ue.crnti(), old_crnti);
    EXPECT_TRUE(l.bs_b.ue_connected(l.ue.crnti()));
    EXPECT_FALSE(l.bs_a.ue_connected(l.ue.crnti()));
    // Registration and security context survived the migration.
    EXPECT_TRUE(l.ue.registered());
    EXPECT_EQ(l.ue.nas().assigned_tmsi(), tmsi_before);

    // The user plane now flows through cell 2 only (cell 1 lost the UE).
    for (int i = 0; i < 3; ++i) {
        l.ue.send_app_data({'H', 'O', static_cast<uint8_t>('0' + i)});
        l.pump(40);
    }
    EXPECT_GE(l.ue.app_rx_count(), 2u);
}

TEST(E2eNodes, AmfArbitratedHandoverBetweenCells) {
    // M15: with a separated core the handover is arbitrated by the AMF —
    // no X2-like coordinator is wired. HO_REQUIRED -> HO_COMMAND (target
    // prepares) -> HO_NOTIFY/HO_PREPARED (source notifies the UE).
    // One AMF whose link fans out to both gNBs (multi-peer InMemoryCnLink).
    cn::InMemoryCnLink ng_amf_side, ng_gnb_a_side, ng_gnb_b_side;
    cn::Amf amf{ng_amf_side};
    ng_gnb_a_side.connect_to(&ng_amf_side);
    ng_gnb_b_side.connect_to(&ng_amf_side);

    core::BsNodeConfig cfg_a; cfg_a.cell_id = 1;
    core::BsNodeConfig cfg_b; cfg_b.cell_id = 2;
    core::BsNode bs_a(make_cell(1, 0));
    core::BsNode bs_b(make_cell(2, 1));

    core::BsNode::CnEndpoints ep_a;
    ep_a.amf = &ng_gnb_a_side; ep_a.gnb_cell = 1;
    core::BsNode::CnEndpoints ep_b;
    ep_b.amf = &ng_gnb_b_side; ep_b.gnb_cell = 2;
    bs_a.attach_core(ep_a);
    bs_b.attach_core(ep_b);

    core::UeNodeConfig uc;
    uc.meas_period_ms = 100;
    core::UeNode ue(uc);

    ue.set_air_send([&](const std::vector<uint8_t>& b) {
        bs_a.on_air_bits(b);
        bs_b.on_air_bits(b);
    });
    bs_a.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });
    bs_b.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs_a.tick(clock);
            bs_b.tick(clock);
        }
    };

    pump(10);
    bs_a.start_broadcast();
    ue.attach();
    for (int i = 0; i < 60 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());
    ASSERT_EQ(ue.serving_cell(), 1u);
    const uint32_t tmsi_before = ue.nas().assigned_tmsi();

    // Light up cell 2, then darken cell 1: measurement-report driven HO,
    // arbitrated entirely through the AMF.
    bs_b.start_broadcast();
    pump(300);
    bs_a.set_sib_enabled(false);
    bool ho_done = false;
    for (int i = 0; i < 200 && !ho_done; ++i) {
        pump(10);
        ho_done = ue.serving_cell() == 2u &&
                  ue.rrc_state() == rrc::UeState::CONNECTED;
    }
    EXPECT_TRUE(ho_done);
    EXPECT_NE(ue.crnti(), 0u);
    EXPECT_TRUE(bs_b.ue_connected(ue.crnti()));
    // Registration survived; user plane still flows (UPF not wired here, so
    // just verify the RRC/registration state).
    EXPECT_TRUE(ue.registered());
    EXPECT_EQ(ue.nas().assigned_tmsi(), tmsi_before);
}

TEST(E2eNodes, CellTargetedAttachStaysOnSelectedCell) {
    // M22: with both cells audible the UE camps on the strongest and its
    // RACH/SetupRequest is answered ONLY by that cell — the foreign cell
    // must hold no context for the UE (preamble partitioning + setup gate).
    core::BsNode bs_a(make_cell(1, 0)), bs_b(make_cell(2, 1));
    core::UeNodeConfig uc;
    uc.imsi = "460011234567890";
    uc.rach.preamble_index = 42;
    core::UeNode ue(uc);
    ue.set_air_send([&](const std::vector<uint8_t>& b) {
        bs_a.on_air_bits(b);
        bs_b.on_air_bits(b);
    });
    bs_a.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });
    bs_b.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs_a.tick(clock);
            bs_b.tick(clock);
        }
    };
    auto pump_until = [&](auto pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    };
    pump(10);
    bs_a.start_broadcast();
    pump(300); // cell 1 clearly stronger before cell 2 appears
    bs_b.start_broadcast();
    pump(300);
    ue.attach();
    ASSERT_TRUE(pump_until([&] { return ue.registered(); }, 800));
    EXPECT_EQ(ue.serving_cell(), 1u);
    EXPECT_TRUE(bs_a.ue_connected(ue.crnti()));
    // The foreign cell never answered: no RRC context for this UE at all.
    EXPECT_EQ(bs_b.rrc().find_ue(ue.crnti()), nullptr);
    ue.detach();
    pump(100);
}

TEST(E2eNodes, XnHandoverMovesUeAndKeepsUserPlane) {
    // M22 capstone path in-process: two gNBs linked over Xn (in-memory
    // carrier; the live demo uses UdpCnLink). ue1 camps on cell 1 with an
    // active voice call to ue2; cell 1 then vanishes FOR UE1 ONLY
    // (broadcasts filtered out, unicast sneaks through — the deterministic
    // equivalent of the channel sim's "bad" profile). Measurement reports
    // drive an Xn handover; registration, keys and the call survive, and
    // U2U traffic crosses cells over Xn forwarding.
    cn::InMemoryCnLink xn_a, xn_b;
    xn_a.connect_to(&xn_b);
    core::BsNode bs_a(make_cell(1, 0)), bs_b(make_cell(2, 1));
    bs_a.attach_xn(&xn_a, 2);
    bs_b.attach_xn(&xn_b, 1);

    auto uecfg = [](const char* imsi, uint32_t preamble) {
        core::UeNodeConfig c;
        c.imsi = imsi;
        c.rach.preamble_index = static_cast<mac::PreambleIndex>(preamble);
        return c;
    };
    core::UeNode ue1(uecfg("460011234567890", 42));
    core::UeNode ue2(uecfg("460011234567891", 43));
    bool dark_for_ue1 = false;
    ue1.set_air_send([&](const std::vector<uint8_t>& b) {
        bs_a.on_air_bits(b);
        bs_b.on_air_bits(b);
    });
    ue2.set_air_send([&](const std::vector<uint8_t>& b) {
        bs_a.on_air_bits(b);
        bs_b.on_air_bits(b);
    });
    bs_a.set_air_send([&](const std::vector<uint8_t>& b) {
        if (dark_for_ue1) {
            // Cell 1 vanished for ue1: broadcasts die, unicast gets
            // through (mirrors the channel sim's per-(UE,cell) "bad").
            std::vector<uint8_t> bytes;
            core::AirFrame f;
            if (core::unpack_air_bits(b, bytes) &&
                core::decode_frame(bytes.data(), bytes.size(), f) &&
                f.rnti == mac::RNTI_BROADCAST) {
                // dropped for ue1
            } else {
                ue1.on_air_bits(b);
            }
        } else {
            ue1.on_air_bits(b);
        }
        ue2.on_air_bits(b);
    });
    bs_b.set_air_send([&](const std::vector<uint8_t>& b) {
        ue1.on_air_bits(b);
        ue2.on_air_bits(b);
    });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue1.tick(clock);
            ue2.tick(clock);
            bs_a.tick(clock);
            bs_b.tick(clock);
        }
    };
    auto pump_until = [&](auto pred, uint32_t max_ms) {
        for (uint32_t e = 0; e < max_ms && !pred(); e += 10) pump(10);
        return pred();
    };

    pump(10);
    bs_a.start_broadcast();
    ue1.set_autoanswer(0);
    ue2.set_autoanswer(0);
    ue1.attach();
    ASSERT_TRUE(pump_until([&] { return ue1.registered(); }, 600));
    ue2.attach();
    ASSERT_TRUE(pump_until([&] { return ue2.registered(); }, 600));
    bs_b.start_broadcast();
    pump(300); // both cells audible; serving stays cell 1
    ASSERT_EQ(ue1.serving_cell(), 1u);

    // Voice call ue1 -> ue2 on cell 1.
    ue1.start_call(app::MediaKind::VOICE, "460011234567891");
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 2; }, 400));
    ue2.answer();
    ASSERT_TRUE(pump_until([&] { return ue2.stream_rx_count() > 3; }, 1000));

    // Cell 1 goes dark for ue1: meas reports lack the serving cell ->
    // bs_a hands ue1 over to cell 2 over Xn.
    const uint16_t old_crnti = ue1.crnti();
    const uint32_t rx_before = ue2.stream_rx_count();
    dark_for_ue1 = true;
    ASSERT_TRUE(pump_until(
        [&] {
            return ue1.serving_cell() == 2u &&
                   ue1.rrc_state() == rrc::UeState::CONNECTED;
        },
        4000));
    EXPECT_NE(ue1.crnti(), old_crnti);
    EXPECT_TRUE(bs_b.ue_connected(ue1.crnti()));
    EXPECT_TRUE(ue1.registered()); // registration carried over

    // The call survives: media keeps flowing into ue2 (ue1 UL -> bs_b ->
    // Xn -> bs_a -> ue2).
    ASSERT_TRUE(pump_until(
        [&] { return ue2.stream_rx_count() > rx_before + 5; }, 3000));

    // Cross-cell U2U after the move: ue2 (cell 1) texts ue1 (cell 2) —
    // bs_a forwards over Xn to bs_b.
    ue2.send_msg("460011234567890", "across-cells");
    ASSERT_TRUE(pump_until([&] { return ue1.msg_rx_count() > 0; }, 2000));
    EXPECT_EQ(ue1.last_msg_text(), "across-cells");

    ue1.end_call();
    ASSERT_TRUE(pump_until([&] { return ue2.call_state() == 0; }, 2000));
    ue1.detach();
    ue2.detach();
    pump(200);
}

TEST(E2eNodes, PagingTriggersIdleUeServiceRequest) {
    Link link;
    link.boot();
    link.ue.attach();
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }));

    link.ue.detach();
    link.pump(40);
    ASSERT_FALSE(link.ue.registered());

    // Network-originated reachability: page the IMSI in the next SIB.
    link.bs.page("460011234567890");
    ASSERT_TRUE(link.pump_until([&] { return link.ue.registered(); }, 2000));
    EXPECT_NE(link.ue.nas().assigned_tmsi(), 0u);
}

TEST(E2eNodes, RadioLinkFailureReestablishmentKeepsNasContext) {
    core::BsNode bs;
    core::UeNodeConfig cfg;
    cfg.radio_link_failure_ms = 300;
    cfg.meas_period_ms = 0; // keep the scenario minimal
    core::UeNode ue(cfg);

    bool mute = false;
    ue.set_air_send([&](const std::vector<uint8_t>& b) { bs.on_air_bits(b); });
    bs.set_air_send([&](const std::vector<uint8_t>& b) {
        if (!mute) ue.on_air_bits(b);
    });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs.tick(clock);
        }
    };
    pump(10);
    bs.start_broadcast();
    ue.attach();
    for (int i = 0; i < 60 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());
    const uint32_t tmsi = ue.nas().assigned_tmsi();
    ue.start_traffic(50);
    pump(200);
    ASSERT_GT(ue.app_rx_count(), 0u);

    // Air goes dead: RLF fires, the UE re-establishes instead of re-attaching.
    mute = true;
    pump(700);
    mute = false;

    for (int i = 0; i < 80 && ue.rrc_state() != rrc::UeState::CONNECTED; ++i) {
        pump(10);
    }
    EXPECT_EQ(ue.rrc_state(), rrc::UeState::CONNECTED);
    EXPECT_TRUE(ue.registered());
    EXPECT_EQ(ue.nas().assigned_tmsi(), tmsi); // NAS context preserved
    pump(300);                                 // buffered traffic drains
    EXPECT_GT(ue.app_rx_count(), 1u);
}

TEST(E2eNodes, ReestablishmentFallsBackToFullAttachAfterBlsRestart) {
    core::BsNode bs_a; // dies mid-scenario (context lost)
    core::BsNode bs_b; // fresh node takes over the air
    core::UeNodeConfig cfg;
    cfg.radio_link_failure_ms = 300;
    cfg.meas_period_ms = 0;
    core::UeNode ue(cfg);

    core::BsNode* rx_side = &bs_a;
    ue.set_air_send([&](const std::vector<uint8_t>& b) { rx_side->on_air_bits(b); });
    bs_a.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            bs_a.tick(clock);
            bs_b.tick(clock);
        }
    };
    pump(10);
    bs_a.start_broadcast();
    ue.attach();
    for (int i = 0; i < 60 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());
    const uint32_t old_tmsi = ue.nas().assigned_tmsi();

    // gNB restart: the old instance goes silent AND stops receiving.
    rx_side = &bs_b;
    bs_a.set_air_send([](const std::vector<uint8_t>&) {});
    bs_b.set_air_send([](const std::vector<uint8_t>&) {}); // dead air first

    pump(700); // RLF window: UE declares failure and starts re-establishing

    // The fresh node takes over transmission; it has no context for the
    // re-establishment request -> FAILURE -> full attach against bs_b.
    // NOTE: the UE stays "registered" throughout (RLF keeps the NAS), so
    // the completion signal is bs_b gaining the registration.
    bs_b.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });
    bs_b.start_broadcast();

    // bs_b's counter flips as soon as the accept is *enqueued*; keep pumping
    // afterwards so the UE actually receives it and lands REGISTERED.
    // (The UE's own NAS may stay "registered" across RLF, so it is not a
    // usable completion signal here.)
    for (int i = 0; i < 250 && bs_b.registered_ue_count() == 0u; ++i) {
        pump(10);
    }
    ASSERT_GE(bs_b.registered_ue_count(), 1u);
    pump(300);
    ASSERT_TRUE(ue.registered());
    // TMSI pools are per-node, so equality proves nothing; prove the fresh
    // core owns the session by round-tripping user-plane data through it.
    ue.send_app_data({'F', 'B', '1'});
    pump(80);
    EXPECT_GE(ue.app_rx_count(), 1u);
}

// ---- M15: core-network separation -------------------------------------------

namespace {

// gNB + external AMF/UPF entities wired over in-memory links. The embedded
// NasBs and the local echo path are bypassed; NAS tunnels to the AMF and the
// user plane terminates at the UPF anchor.
struct CnLink {
    core::BsNode bs;
    cn::InMemoryCnLink amf_to_bs, bs_to_amf;
    cn::InMemoryCnLink upf_to_bs, bs_to_upf;
    cn::Amf amf{amf_to_bs};
    cn::Upf upf{upf_to_bs};

    CnLink() {
        // gNB -> AMF carrier and back.
        bs_to_amf.connect_to(&amf_to_bs);
        // gNB -> UPF carrier and back.
        bs_to_upf.connect_to(&upf_to_bs);

        core::BsNode::CnEndpoints ep;
        ep.amf = &bs_to_amf;
        ep.upf = &bs_to_upf;
        bs.attach_core(ep);

        // UPF echo: downlink data bounces back to the UE (network-side ping).
        upf.set_ul_sink([this](uint32_t tmsi, const std::vector<uint8_t>& pdu) {
            upf.send_downlink(tmsi, pdu);
        });
    }
};

} // namespace

TEST(E2eNodes, SeparatedCoreAttachAndUserPlane) {
    // M15: attach with the control plane in the AMF and the user plane
    // anchored at the UPF. The gNB must not use its embedded NasBs.
    CnLink c;
    core::UeNode ue;
    ue.set_air_send([&](const std::vector<uint8_t>& b) { c.bs.on_air_bits(b); });
    c.bs.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            c.bs.tick(clock);
        }
    };
    pump(10);
    c.bs.start_broadcast();
    ue.attach();
    for (int i = 0; i < 40 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());

    // Registration lives in the AMF now.
    EXPECT_GE(c.amf.registered_count(), 1u);
    EXPECT_EQ(c.bs.registered_ue_count(), 1u);

    // User plane: UL reaches the UPF, its echo comes back routed DL.
    for (int i = 0; i < 3; ++i) {
        ue.send_app_data({'N', '1', '5', static_cast<uint8_t>('0' + i)});
        pump(40);
    }
    EXPECT_EQ(c.upf.session_count(), 1u);
    EXPECT_EQ(ue.app_tx_count(), 3u);
    EXPECT_EQ(ue.app_rx_count(), 3u);
}

TEST(E2eNodes, SeparatedCoreAuthenticatedAttach) {
    // M12 semantics preserved through the split: subscriber key lives in the
    // AMF's HSS, AUTH challenge/response crosses the NG link, and the session
    // key is delivered to the gNB afterwards so user-plane crypto still works.
    CnLink c;
    core::UeNodeConfig ue_cfg;
    const std::string imsi = "460017777777777";
    ue_cfg.imsi = imsi;
    core::UeNode ue(ue_cfg);

    std::array<uint8_t, crypto::kKey256Size> usim_key{};
    for (size_t i = 0; i < usim_key.size(); ++i) {
        usim_key[i] = static_cast<uint8_t>(0x30 + i);
    }
    ue.nas().set_usim_key(usim_key);
    c.amf.add_subscriber(imsi, usim_key); // HSS provisioning at the AMF

    ue.set_air_send([&](const std::vector<uint8_t>& b) { c.bs.on_air_bits(b); });
    c.bs.set_air_send([&](const std::vector<uint8_t>& b) { ue.on_air_bits(b); });

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            c.bs.tick(clock);
        }
    };
    pump(10);
    c.bs.start_broadcast();
    ue.attach();
    for (int i = 0; i < 60 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());
    EXPECT_TRUE(ue.nas().authenticated());

    // Session key reached the flow: encrypted round-trip via the UPF.
    for (int i = 0; i < 4; ++i) {
        ue.send_app_data({'K', 'E', 'Y', static_cast<uint8_t>('0' + i)});
        pump(40);
    }
    EXPECT_EQ(ue.app_tx_count(), 4u);
    EXPECT_EQ(ue.app_rx_count(), 4u);
}

TEST(E2eNodes, SeparatedCoreSurvivesBsRestartViaAnchor) {
    // The point of the split: kill the gNB, bring up a fresh one — the AMF
    // registration table survives, and the UPF keeps routing once the new
    // gNB re-registers the UE's route.
    // Shared carrier links: both gNB instances talk to the SAME AMF/UPF, so
    // the UPF's downlink reaches whichever gNB is currently live.
    cn::InMemoryCnLink amf_to_bs, bs_to_amf;
    cn::InMemoryCnLink upf_to_bs, bs_to_upf;
    cn::Amf amf{amf_to_bs}; // single AMF behind the shared link
    cn::Upf upf{upf_to_bs};
    upf.set_ul_sink([&](uint32_t tmsi, const std::vector<uint8_t>& pdu) {
        upf.send_downlink(tmsi, pdu);
    });
    bs_to_amf.connect_to(&amf_to_bs);
    bs_to_upf.connect_to(&upf_to_bs);

    core::BsNodeConfig cfg;
    cfg.cell_id = 1;
    core::UeNodeConfig ue_cfg;
    ue_cfg.radio_link_failure_ms = 200; // detect the outage, re-attach
    core::UeNode ue(ue_cfg);

    // Indirection so the UE's air_send lambda survives gNB replacement.
    core::BsNode* live_bs = new core::BsNode(cfg);
    bool air_alive = true;
    ue.set_air_send([&](const std::vector<uint8_t>& b) {
        if (live_bs && air_alive) live_bs->on_air_bits(b);
    });
    auto wire_dl = [&](core::BsNode& gnb) {
        gnb.set_air_send([&](const std::vector<uint8_t>& b) {
            if (air_alive) ue.on_air_bits(b);
        });
    };

    uint32_t clock = 1000;
    auto pump = [&](uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 10) {
            clock += 10;
            ue.tick(clock);
            if (live_bs && air_alive) live_bs->tick(clock);
        }
    };

    core::BsNode::CnEndpoints ep;
    ep.amf = &bs_to_amf;
    ep.upf = &bs_to_upf;
    wire_dl(*live_bs);
    live_bs->attach_core(ep);
    pump(10);
    live_bs->start_broadcast();
    ue.attach();
    for (int i = 0; i < 40 && !ue.registered(); ++i) pump(10);
    ASSERT_TRUE(ue.registered());
    EXPECT_EQ(amf.registered_count(), 1u);

    // gNB dies; AMF/UPF keep their state. Pump through the outage window so
    // the UE's RLF watchdog fires against the silent air.
    air_alive = false;
    pump(500);
    delete live_bs;
    live_bs = nullptr;

    // Fresh gNB instance takes over the same cell identity.
    live_bs = new core::BsNode(cfg);
    air_alive = true;
    wire_dl(*live_bs); // rebind the DL capture to this instance
    core::BsNode::CnEndpoints ep2;
    ep2.amf = &bs_to_amf;
    ep2.upf = &bs_to_upf;
    live_bs->attach_core(ep2);
    live_bs->start_broadcast();

    // The UE is still CONNECTED to the dead context, so attach() alone is a
    // no-op: pump until the RLF watchdog fires (NAS drops), re-establishment
    // fails (fresh gNB has no AS context), and the fallback full attach
    // restores REGISTERED + CONNECTED with fresh AS state.
    bool dropped = false;
    for (int i = 0; i < 400; ++i) {
        pump(10);
        if (!dropped && !ue.registered()) dropped = true;
        if (dropped && ue.registered() &&
            ue.rrc_state() == rrc::UeState::CONNECTED) {
            break;
        }
    }
    ASSERT_TRUE(dropped) << "RLF never deregistered the UE";
    ASSERT_TRUE(ue.registered());
    pump(200); // let any in-flight RLC/HARQ state settle

    ue.send_app_data({'A', 'N', 'C', 'H'});
    for (int i = 0; i < 8 && ue.app_rx_count() == 0u; ++i) pump(20);
    EXPECT_GE(ue.app_rx_count(), 1u); // anchored data path restored

    delete live_bs;
}
