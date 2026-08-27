#include "rrc/rrc_ue.h"
#include "rrc/rrc_bs.h"
#include "nas/nas_ue.h"
#include "nas/nas_bs.h"
#include "mac/rach_ue.h"
#include "mac/rach_bs.h"
#include <gtest/gtest.h>
#include <vector>

using namespace rrc;
using namespace nas;
using namespace mac;

struct PipelineMsg {
    std::string layer;
    std::vector<uint8_t> data;
};

TEST(FullAttach, ColdStartToRegistered) {
    RrcUe rrc_ue;
    RrcBs rrc_bs;
    NasUe nas_ue;
    NasBs nas_bs;
    RachUe rach_ue;
    RachBs rach_bs;

    std::vector<PipelineMsg> ue_to_bs;
    std::vector<PipelineMsg> bs_to_ue;

    auto ue_send = [&](const std::string& layer, const std::vector<uint8_t>& data) {
        ue_to_bs.push_back({layer, data});
    };
    auto bs_send = [&](const std::string& layer, const std::vector<uint8_t>& data) {
        bs_to_ue.push_back({layer, data});
    };

    rrc_ue.set_send_callback([&](const std::vector<uint8_t>& pdu) { ue_send("RRC", pdu); });
    rrc_bs.set_send_callback([&](uint16_t, const std::vector<uint8_t>& pdu) { bs_send("RRC", pdu); });
    nas_ue.set_send_callback([&](const std::vector<uint8_t>& pdu) { ue_send("NAS", pdu); });
    nas_bs.set_send_callback([&](uint32_t, const std::vector<uint8_t>& pdu) { bs_send("NAS", pdu); });
    rach_ue.set_send_callback([&](RachMsgType, const std::vector<uint8_t>& pdu) { ue_send("RACH", pdu); });
    rach_bs.set_send_callback([&](RachMsgType, const std::vector<uint8_t>& pdu) { bs_send("RACH", pdu); });

    // Step 1: BS broadcasts MIB/SIB1, UE receives
    auto mib = rrc_bs.broadcast_mib();
    auto sib1 = rrc_bs.broadcast_sib1();
    rrc_ue.on_mib_received(mib);
    rrc_ue.on_sib1_received(sib1);

    // Step 2: RACH 4-step
    rach_ue.start_rach();
    ASSERT_EQ(ue_to_bs.size(), 1u);
    EXPECT_EQ(ue_to_bs[0].layer, "RACH");

    PreambleIndex preamble = ue_to_bs[0].data[1];
    rach_bs.on_prach_received(preamble);
    ASSERT_EQ(bs_to_ue.size(), 1u);
    EXPECT_EQ(bs_to_ue[0].layer, "RACH");

    auto& msg2 = bs_to_ue[0].data;
    RaRnti ra_rnti = msg2[1] | (msg2[2] << 8);
    rach_ue.on_rar_received(ra_rnti, msg2[3], msg2[4]);
    ASSERT_EQ(ue_to_bs.size(), 2u);

    rach_bs.on_msg3_received(ra_rnti, ue_to_bs[1].data);
    ASSERT_EQ(bs_to_ue.size(), 2u);

    auto& msg4 = bs_to_ue[1].data;
    uint16_t crnti = msg4[1] | (msg4[2] << 8);
    rach_ue.on_contention_resolve(crnti, ra_rnti);
    EXPECT_EQ(rach_ue.state(), RachState::CONNECTED);

    // Step 3: RRC Connection Setup
    rrc_ue.start_connection();
    ASSERT_EQ(ue_to_bs.size(), 3u);
    EXPECT_EQ(ue_to_bs[2].layer, "RRC");

    rrc_bs.handle_message(crnti, ue_to_bs[2].data);
    ASSERT_EQ(bs_to_ue.size(), 3u);

    rrc_ue.on_message(bs_to_ue[2].data);
    EXPECT_EQ(rrc_ue.state(), rrc::UeState::CONNECTED);

    ASSERT_EQ(ue_to_bs.size(), 4u);
    rrc_bs.handle_message(crnti, ue_to_bs[3].data);
    EXPECT_TRUE(rrc_bs.is_ue_connected(crnti));

    // Step 4: NAS Attach
    nas_ue.send_attach_request("460011234567890");
    EXPECT_EQ(nas_ue.state(), nas::UeState::REGISTERING);
    ASSERT_EQ(ue_to_bs.size(), 5u);
    EXPECT_EQ(ue_to_bs[4].layer, "NAS");

    nas_bs.handle_message(0, ue_to_bs[4].data);
    ASSERT_EQ(bs_to_ue.size(), 4u);

    nas_ue.on_message(bs_to_ue[3].data);
    EXPECT_EQ(nas_ue.state(), nas::UeState::REGISTERED);

    // Final verification
    EXPECT_NE(nas_ue.assigned_tmsi(), 0u);
    EXPECT_TRUE(nas_bs.is_ue_registered(nas_ue.assigned_tmsi()));
}

TEST(FullAttach, AllStatesProgressCorrectly) {
    RrcUe rrc_ue;
    NasUe nas_ue;
    RachUe rach_ue;

    EXPECT_EQ(rrc_ue.state(), rrc::UeState::IDLE);
    EXPECT_EQ(nas_ue.state(), nas::UeState::DEREGISTERED);
    EXPECT_EQ(rach_ue.state(), RachState::IDLE);

    rach_ue.start_rach();
    EXPECT_EQ(rach_ue.state(), RachState::WAIT_RAR);
}
