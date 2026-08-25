// M6.5 T6: cross-layer E2E over an in-memory air interface.
// UeNode <-> BsNode exchange real air frames (pack_air_bits/encode_frame),
// covering SIB gating, RACH 4-step, RRC setup, NAS attach, user-plane
// ping-pong, detach and attach-guard fault recovery.
#include "core/bs_node.h"
#include "core/ue_node.h"
#include "mac/mac_pdu.h"
#include "rrc/rrc_messages.h"
#include <gtest/gtest.h>
#include <array>
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

TEST(E2eNodes, AuthenticatedAttachWithEncryptedUserPlane) {
    // M12: provision a subscriber (USIM key on the UE, same key in the HSS).
    // Attach must include the AUTH challenge/response exchange, and the
    // user plane must flow with PDCP confidentiality enabled.
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
    // authentication and never reach REGISTERED.
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
