# 垂直直通测试

位置: `stack/tests/test_vertical.cpp` (4 个测试)

## 测试详解

这些测试验证 PDCP → RLC → MAC → PHY 各层的垂直集成，数据通过完整协议栈传输并在 AWGN 信道后恢复。

### Vertical.PdcpRlcMacRoundTrip

```
PDCP tx → RLC tm_tx → MAC build_pdu → MAC parse_pdu → RLC tm_rx → PDCP rx
```

验证纯上三层 (无 PHY) 的 PDU 正确性。

### Vertical.PdcpRlcMacPhyRoundTrip

```
PDCP tx → RLC tm_tx → MAC build_pdu → PHY phy_tx → AWGN(30dB) → PHY phy_rx → MAC parse_pdu → RLC tm_rx → PDCP rx
```

验证含 PHY 和 AWGN 的全栈 round-trip，BER=0。

### Vertical.MultipleSdusVertical

多 SDU 经完整协议栈传输，验证 MAC 多 LCID 分复用正确。

### Vertical.LargePayloadVertical

512 字节大载荷经全栈传输，验证大数据量下的可靠性。

## 关键点

- `phy_rx()` 需要传入 `n_data_bits` 参数 (原始 SDU 的比特数)
- AWGN SNR=30dB 足以保证 QPSK 无误码
- MAC 层需从解析结果中提取正确 LCID 的 SDU
