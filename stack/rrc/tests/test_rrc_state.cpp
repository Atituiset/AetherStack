#include "rrc/rrc_types.h"
#include "rrc/rrc_messages.h"
#include <gtest/gtest.h>

using namespace rrc;

TEST(RrcMessages, MibEncodeDecodeRoundTrip) {
    Mib mib;
    mib.sfn = 123;
    mib.dl_bandwidth = 50;
    mib.phich_config = 1;
    auto encoded = mib.encode();
    auto decoded = Mib::decode(encoded);
    EXPECT_EQ(decoded.sfn, mib.sfn);
    EXPECT_EQ(decoded.dl_bandwidth, mib.dl_bandwidth);
    EXPECT_EQ(decoded.phich_config, mib.phich_config);
}

TEST(RrcMessages, Sib1EncodeDecodeRoundTrip) {
    Sib1 sib1;
    sib1.plmn_id = "46001";
    sib1.tac = 42;
    sib1.cell_id = 100;
    auto encoded = sib1.encode();
    auto decoded = Sib1::decode(encoded);
    EXPECT_EQ(decoded.plmn_id, sib1.plmn_id);
    EXPECT_EQ(decoded.tac, sib1.tac);
    EXPECT_EQ(decoded.cell_id, sib1.cell_id);
}

TEST(RrcMessages, RrcMessageEncodeDecodeRoundTrip) {
    RrcMessage msg;
    msg.msg_type = RrcMessageType::SETUP_REQUEST;
    msg.value = {0x01, 0x02, 0x03};
    auto encoded = msg.encode();
    auto decoded = RrcMessage::decode(encoded);
    EXPECT_EQ(decoded.msg_type, RrcMessageType::SETUP_REQUEST);
    EXPECT_EQ(decoded.value, msg.value);
}

TEST(RrcMessages, GenerateMibDefaults) {
    auto mib = generate_mib(5);
    EXPECT_EQ(mib.sfn, 5);
    EXPECT_EQ(mib.dl_bandwidth, 50);
}

TEST(RrcMessages, GenerateSib1Defaults) {
    auto sib1 = generate_sib1();
    EXPECT_EQ(sib1.plmn_id, "46001");
    EXPECT_EQ(sib1.tac, 1);
}
