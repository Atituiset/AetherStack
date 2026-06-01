#include "rrc/rrc_ue.h"
#include "rrc/rrc_bs.h"
#include <gtest/gtest.h>
#include <vector>

using namespace rrc;

TEST(RrcConnection, UeInitialStateIsIdle) {
    RrcUe ue;
    EXPECT_EQ(ue.state(), UeState::IDLE);
}

TEST(RrcConnection, FullRrcSetupHandshake) {
    RrcUe ue;
    RrcBs bs;

    std::vector<std::vector<uint8_t>> ue_to_bs;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> bs_to_ue;

    ue.set_send_callback([&](const std::vector<uint8_t>& pdu) {
        ue_to_bs.push_back(pdu);
    });
    bs.set_send_callback([&](uint16_t rnti, const std::vector<uint8_t>& pdu) {
        bs_to_ue.push_back({rnti, pdu});
    });

    ue.start_connection();
    EXPECT_EQ(ue.state(), UeState::CONNECTING);
    ASSERT_EQ(ue_to_bs.size(), 1u);

    bs.handle_message(0, ue_to_bs[0]);
    ASSERT_EQ(bs_to_ue.size(), 1u);
    auto [crnti, setup_pdu] = bs_to_ue[0];

    ue.on_message(setup_pdu);
    EXPECT_EQ(ue.state(), UeState::CONNECTED);
    EXPECT_EQ(ue.assigned_crnti(), crnti);

    auto setup_complete = ue_to_bs[1];
    bs.handle_message(crnti, setup_complete);
    EXPECT_TRUE(bs.is_ue_connected(crnti));
}

TEST(RrcConnection, StartConnectionIgnoredIfNotIdle) {
    RrcUe ue;
    ue.start_connection();
    EXPECT_EQ(ue.state(), UeState::CONNECTING);
    ue.start_connection();
    EXPECT_EQ(ue.state(), UeState::CONNECTING);
}

TEST(RrcConnection, BsBroadcastsMibAndSib1) {
    RrcBs bs;
    auto mib = bs.broadcast_mib();
    auto sib1 = bs.broadcast_sib1();
    EXPECT_EQ(mib.sfn, 0);
    EXPECT_EQ(sib1.plmn_id, "46001");
}

TEST(RrcConnection, UeReceivesSystemInfo) {
    RrcUe ue;
    auto mib = generate_mib(42);
    auto sib1 = generate_sib1();
    ue.on_mib_received(mib);
    ue.on_sib1_received(sib1);
    EXPECT_EQ(ue.state(), UeState::IDLE);
}
