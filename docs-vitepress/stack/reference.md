# 协议栈参考

AetherStack 实现了 5G NR 协议栈的简化子集，遵循自底向上的分层架构。每一层通过明确的 C++ 命名空间隔离，层间通过函数调用传递 `std::vector<uint8_t>` 格式的 PDU/SDU。

## 层次对照表

| AetherStack 层 | 3GPP 对应 | 命名空间 | 核心类/函数 | 测试数 |
|----------------|-----------|---------|------------|--------|
| PHY | TS 38.211 | `phy` | `qpsk_modulate/demodulate`, `ofdm_tx/rx`, `phy_tx/rx` | 15 |
| MAC | TS 38.321 | `mac` | `build_pdu/parse_pdu`, `RachUe`, `RachBs` | 12 |
| RLC | TS 38.322 | `rlc` | `tm_tx/tm_rx` | 4 |
| PDCP | TS 38.323 | `pdcp` | `tx/rx` (2 字节头) | 5 |
| RRC | TS 38.331 | `rrc` | `RrcUe`, `RrcBs`, `Mib`, `Sib1`, `RrcMessage` | 10 |
| NAS | TS 24.501 | `nas` | `NasUe`, `NasBs`, `NasMessage` | 4 |
| App | — | `app` | `AppLayer` | 4+3 |
| Common | — | `logging`, `transport` | `Logger`, `UdpTransport` | 3 |

## 数据封装关系

```
App Data (原始字节)
  │
  ├─ PDCP tx(): 添加 2 字节头 [ver|rsvd][seq]
  │    PDCP PDU = [header(2)] + [SDU]
  │
  ├─ RLC tm_tx(): 透传 (PDU = SDU)
  │    RLC PDU = PDCP PDU (不变)
  │
  ├─ MAC build_pdu(): 添加子头 [R|F|LCID][L][L_hi]
  │    MAC PDU = [subheader] + [RLC PDU] [+ padding]
  │
  └─ PHY phy_tx(): QPSK → OFDM → 时域 IQ
       IQ Samples = OFDM(CP + IFFT(QPSK(bits)))
```

## 状态机总览

| 层 | UE 侧状态 | BS 侧行为 |
|----|----------|----------|
| MAC RACH | IDLE → WAIT_RAR → WAIT_CR → CONNECTED | 监听 PRACH, 分配 RA-RNTI/C-RNTI |
| RRC | IDLE → CONNECTING → CONNECTED | 处理 Setup, 回复 Setup+Complete |
| NAS | DEREGISTERED → REGISTERING → REGISTERED | 处理 Attach, 分配 TMSI |
