#include "mac/mac_pdu.h"
#include "mac/rach_ue.h"
#include "mac/rach_bs.h"
#include <gtest/gtest.h>
#include <vector>

using namespace mac;

// --- MAC PDU tests ---

TEST(MacPdu, SingleSduRoundTrip) {
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> sdus = {
        {1, {0xAA, 0xBB, 0xCC, 0xDD}},
    };
    auto pdu = build_pdu(sdus);
    auto parsed = parse_pdu(pdu);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].first, 1);
    EXPECT_EQ(parsed[0].second, sdus[0].second);
}

TEST(MacPdu, MultipleSdusRoundTrip) {
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> sdus = {
        {1, {0x01, 0x02}},
        {3, {0x03, 0x04, 0x05}},
        {5, {0x06}},
    };
    auto pdu = build_pdu(sdus);
    auto parsed = parse_pdu(pdu);
    ASSERT_EQ(parsed.size(), sdus.size());
    for (size_t i = 0; i < sdus.size(); ++i) {
        EXPECT_EQ(parsed[i].first, sdus[i].first);
        EXPECT_EQ(parsed[i].second, sdus[i].second);
    }
}

TEST(MacPdu, LargeSduUsesFBit) {
    std::vector<uint8_t> large_payload(300, 0xAB);
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> sdus = {
        {10, large_payload},
    };
    auto pdu = build_pdu(sdus);
    auto parsed = parse_pdu(pdu);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].first, 10);
    EXPECT_EQ(parsed[0].second.size(), 300u);
}

TEST(MacPdu, CcchSduRoundTrip) {
    std::vector<uint8_t> ccch_data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> sdus = {
        {LCID_CCCH, ccch_data},
    };
    auto pdu = build_pdu(sdus);
    auto parsed = parse_pdu(pdu);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].first, LCID_CCCH);
    EXPECT_EQ(parsed[0].second, ccch_data);
}

// --- RACH FSM tests ---

TEST(RachUe, InitialStateIsIdle) {
    RachUe ue;
    EXPECT_EQ(ue.state(), RachState::IDLE);
}

TEST(RachUe, StartRachTransitionsToWaitRar) {
    RachUe ue;
    RachState captured_new = RachState::IDLE;
    ue.set_state_callback([&](RachState old_s, RachState new_s) {
        captured_new = new_s;
    });
    ue.start_rach();
    EXPECT_EQ(ue.state(), RachState::WAIT_RAR);
    EXPECT_EQ(captured_new, RachState::WAIT_RAR);
}

TEST(RachUe, FullFourStepRach) {
    RachUe ue;
    std::vector<RachMsgType> sent_msgs;

    ue.set_send_callback([&](RachMsgType type, const std::vector<uint8_t>&) {
        sent_msgs.push_back(type);
    });

    ue.start_rach();
    EXPECT_EQ(sent_msgs.size(), 1u);
    EXPECT_EQ(sent_msgs[0], RachMsgType::MSG1_PRACH);

    // M22: derive from the configured preamble (the default moved from 42
    // to 10 with cell-partitioned preambles; 0x432A was preamble 42).
    const RaRnti test_ra_rnti = ra_rnti_for_preamble(10);
    ue.on_rar_received(test_ra_rnti, 12, 5);
    EXPECT_EQ(ue.state(), RachState::WAIT_CONTENTION_RESOLVE);
    EXPECT_EQ(sent_msgs.size(), 2u);
    EXPECT_EQ(sent_msgs[1], RachMsgType::MSG3_RRC_REQ);

    ue.on_contention_resolve(0x0001, test_ra_rnti);
    EXPECT_EQ(ue.state(), RachState::CONNECTED);
}

TEST(RachUe, StartRachIgnoredIfNotIdle) {
    RachUe ue;
    ue.start_rach();
    EXPECT_EQ(ue.state(), RachState::WAIT_RAR);
    ue.start_rach();
    EXPECT_EQ(ue.state(), RachState::WAIT_RAR);
}

TEST(RachUe, RarTimeoutRetries) {
    RachConfig config;
    config.max_preamble_transmissions = 3;
    RachUe ue(config);
    int msg1_count = 0;
    ue.set_send_callback([&](RachMsgType type, const std::vector<uint8_t>&) {
        if (type == RachMsgType::MSG1_PRACH) msg1_count++;
    });
    ue.start_rach();
    EXPECT_EQ(msg1_count, 1);
    ue.on_rar_timeout();
    EXPECT_EQ(msg1_count, 2);
    ue.on_rar_timeout();
    EXPECT_EQ(msg1_count, 3);
    ue.on_rar_timeout();
    EXPECT_EQ(ue.state(), RachState::IDLE);
}

TEST(RachBs, FullFourStepBsSide) {
    RachBs bs;
    std::vector<RachMsgType> sent_msgs;
    bs.set_send_callback([&](RachMsgType type, const std::vector<uint8_t>&) {
        sent_msgs.push_back(type);
    });

    // M22: preambles are cell-partitioned; a cell-1 BS answers only its
    // own half (0-31). Preamble 42 targets cell 2 and is now ignored.
    bs.on_prach_received(42);
    EXPECT_TRUE(sent_msgs.empty());
    bs.on_prach_received(10);
    EXPECT_EQ(sent_msgs.size(), 1u);
    EXPECT_EQ(sent_msgs[0], RachMsgType::MSG2_RAR);

    RaRnti ra_rnti = ra_rnti_for_preamble(10);
    bs.on_msg3_received(ra_rnti, {0x01, 0x02});
    EXPECT_EQ(sent_msgs.size(), 2u);
    EXPECT_EQ(sent_msgs[1], RachMsgType::MSG4_CONTENTION_RESOLVE);

    EXPECT_TRUE(bs.is_rach_complete(ra_rnti));
}

TEST(RachBs, UnknownMsg3Ignored) {
    RachBs bs;
    bs.on_msg3_received(0xFFFF, {});
    EXPECT_FALSE(bs.is_rach_complete(0xFFFF));
}

// --- E2E RACH test (UE + BS in memory) ---

TEST(RachE2E, UeBsFourStepHandshake) {
    RachUe ue;
    RachBs bs;

    struct Msg {
        RachMsgType type;
        std::vector<uint8_t> data;
    };
    std::vector<Msg> ue_to_bs;
    std::vector<Msg> bs_to_ue;

    ue.set_send_callback([&](RachMsgType type, const std::vector<uint8_t>& data) {
        ue_to_bs.push_back({type, data});
    });
    bs.set_send_callback([&](RachMsgType type, const std::vector<uint8_t>& data) {
        bs_to_ue.push_back({type, data});
    });

    // Step 1: UE sends MSG1
    ue.start_rach();
    ASSERT_EQ(ue_to_bs.size(), 1u);
    EXPECT_EQ(ue_to_bs[0].type, RachMsgType::MSG1_PRACH);

    // BS receives MSG1, sends MSG2
    PreambleIndex preamble = ue_to_bs[0].data[1];
    bs.on_prach_received(preamble);
    ASSERT_EQ(bs_to_ue.size(), 1u);
    EXPECT_EQ(bs_to_ue[0].type, RachMsgType::MSG2_RAR);

    // UE receives MSG2
    auto& msg2 = bs_to_ue[0].data;
    RaRnti ra_rnti = msg2[1] | (msg2[2] << 8);
    uint16_t ta = msg2[3];
    uint8_t ul_grant = msg2[4];
    ue.on_rar_received(ra_rnti, ta, ul_grant);
    ASSERT_EQ(ue_to_bs.size(), 2u);
    EXPECT_EQ(ue_to_bs[1].type, RachMsgType::MSG3_RRC_REQ);

    // BS receives MSG3, sends MSG4
    auto& msg3 = ue_to_bs[1].data;
    bs.on_msg3_received(ra_rnti, msg3);
    ASSERT_EQ(bs_to_ue.size(), 2u);
    EXPECT_EQ(bs_to_ue[1].type, RachMsgType::MSG4_CONTENTION_RESOLVE);

    // UE receives MSG4
    auto& msg4 = bs_to_ue[1].data;
    uint16_t crnti = msg4[1] | (msg4[2] << 8);
    ue.on_contention_resolve(crnti, ra_rnti);

    EXPECT_EQ(ue.state(), RachState::CONNECTED);
    EXPECT_TRUE(bs.is_rach_complete(ra_rnti));
}
