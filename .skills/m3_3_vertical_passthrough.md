# M3.3 全链路垂直透传测试

## 目标
验证一个数据包穿过完整协议栈的垂直贯通：PDCP→RLC→MAC→PHY→信道→PHY→MAC→RLC→PDCP，两端内容一致。这是第一个跨多层集成验证点。

## 范围
- 发端：Upper SDU → PDCP tx → RLC tm_tx → MAC build_pdu → PHY phy_tx → IQ samples
- 信道：AWGN 仿真（低噪声，SNR=30dB，确保无误码）
- 收端：IQ samples → PHY phy_rx → MAC parse_pdu → RLC tm_rx → PDCP rx → Upper SDU
- 单进程测试，无 UDP

## 测试流程
```cpp
TEST(VerticalPassthrough, PdcpRlcMacPhyRoundTrip) {
    // 1. 准备原始数据
    std::vector<uint8_t> original_sdu = {0x48, 0x65, 0x6C, 0x6C, 0x6F};

    // 2. 发端协议栈
    auto pdcp_pdu = pdcp::tx(original_sdu);        // +2 byte header
    auto rlc_pdu  = rlc::tm_tx(pdcp_pdu);          // pass-through
    auto mac_pdu  = mac::build_pdu({{1, rlc_pdu}}); // +subheader
    auto iq_tx    = phy::phy_tx(mac_pdu);           // OFDM modulate

    // 3. 信道（低噪声 AWGN）
    auto iq_rx    = add_awgn(iq_tx, 30.0);         // SNR=30dB

    // 4. 收端协议栈
    auto mac_rx   = phy::phy_rx(iq_rx);             // OFDM demodulate
    auto sdus     = mac::parse_pdu(mac_rx);          // extract (lcid, sdu)
    auto rlc_sdu  = rlc::tm_rx(sdus[0].second);     // pass-through
    auto final_sdu = pdcp::rx(rlc_sdu);             // strip PDCP header

    // 5. 验证
    EXPECT_EQ(original_sdu, final_sdu);
}
```

## 验证标准
1. 完整链路 round-trip：原始 SDU == 最终 SDU
2. 各层 PDU 长度符合预期（PDCP +2, MAC +subheader, PHY ×80)
3. 多个不同大小的 SDU 均通过
4. 低噪声 (SNR≥30dB) 下无误码

## 依赖
- M3.1 RLC TM
- M3.2 PDCP 透传
- M2 MAC PDU
- M1 PHY 链路

## 产出文件
- `stack/tests/test_vertical.cpp`
