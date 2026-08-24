#include "nas/nas_ue.h"
#include "nas/nas_bs.h"
#include <gtest/gtest.h>
#include <vector>

using namespace nas;

TEST(NasAttach, UeInitialStateIsDeregistered) {
    NasUe ue;
    EXPECT_EQ(ue.state(), UeState::DEREGISTERED);
}

TEST(NasAttach, FullAttachHandshake) {
    NasUe ue;
    NasBs bs;

    std::vector<std::vector<uint8_t>> ue_to_bs;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> bs_to_ue;

    ue.set_send_callback([&](const std::vector<uint8_t>& pdu) {
        ue_to_bs.push_back(pdu);
    });
    bs.set_send_callback([&](uint32_t tmsi, const std::vector<uint8_t>& pdu) {
        bs_to_ue.push_back({tmsi, pdu});
    });

    ue.send_attach_request("460011234567890");
    EXPECT_EQ(ue.state(), UeState::REGISTERING);
    EXPECT_EQ(ue.imsi(), "460011234567890");
    ASSERT_EQ(ue_to_bs.size(), 1u);

    bs.handle_message(0, ue_to_bs[0]);
    ASSERT_EQ(bs_to_ue.size(), 1u);
    auto [tmsi, accept_pdu] = bs_to_ue[0];

    ue.on_message(accept_pdu);
    EXPECT_EQ(ue.state(), UeState::REGISTERED);
    EXPECT_EQ(ue.assigned_tmsi(), tmsi);
    EXPECT_TRUE(bs.is_ue_registered(tmsi));
}

TEST(NasAttach, SendAttachIgnoredIfNotDeregistered) {
    NasUe ue;
    ue.send_attach_request("460011234567890");
    EXPECT_EQ(ue.state(), UeState::REGISTERING);
    ue.send_attach_request("46001999");
    EXPECT_EQ(ue.imsi(), "460011234567890");
}

TEST(NasAttach, UeStateStringMapping) {
    EXPECT_STREQ(ue_state_str(UeState::DEREGISTERED), "DEREGISTERED");
    EXPECT_STREQ(ue_state_str(UeState::REGISTERING), "REGISTERING");
    EXPECT_STREQ(ue_state_str(UeState::REGISTERED), "REGISTERED");
}

TEST(NasDetach, UeDetachDeregistersAndNotifiesBs) {
    NasUe ue;
    NasBs bs;

    std::vector<std::vector<uint8_t>> ue_to_bs;
    ue.set_send_callback([&](const std::vector<uint8_t>& pdu) { ue_to_bs.push_back(pdu); });
    bs.set_send_callback([&](uint32_t, const std::vector<uint8_t>& pdu) { ue.on_message(pdu); });

    ue.send_attach_request("460011234567890");
    ASSERT_EQ(ue.state(), UeState::REGISTERING);
    bs.handle_message(0, ue_to_bs[0]);
    ASSERT_EQ(ue.state(), UeState::REGISTERED);
    ASSERT_NE(ue.assigned_tmsi(), 0u);
    uint32_t tmsi = ue.assigned_tmsi();
    ue_to_bs.clear();

    ue.send_detach();
    EXPECT_EQ(ue.state(), UeState::DEREGISTERED);
    EXPECT_EQ(ue.assigned_tmsi(), 0u);
    ASSERT_EQ(ue_to_bs.size(), 1u);

    bs.handle_message(tmsi, ue_to_bs[0]);
    EXPECT_FALSE(bs.is_ue_registered(tmsi));
}

TEST(NasDetach, DetachIgnoredWhenAlreadyDeregistered) {
    NasUe ue;
    int sends = 0;
    ue.set_send_callback([&](const std::vector<uint8_t>&) { sends++; });
    ue.send_detach();
    EXPECT_EQ(sends, 0);
}
