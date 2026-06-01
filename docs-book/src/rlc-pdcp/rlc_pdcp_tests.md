# RLC/PDCP 测试

## RLC TM 测试 (4 个)

| 测试名 | 验证 |
|--------|------|
| `TmTxReturnsInput` | `tm_tx` 返回值与输入完全相同 |
| `TmRxReturnsInput` | `tm_rx` 返回值与输入完全相同 |
| `RoundTrip` | `tm_rx(tm_tx(data)) == data` |
| `EmptyAndLargeSdu` | 空 SDU 和大 SDU (1000 字节) 透传正确 |

## PDCP 测试 (5 个)

| 测试名 | 验证 |
|--------|------|
| `TxAddsHeader` | 输出 = 输入 + 2 字节, `pdu[0]>>4 == 0` (version) |
| `RxStripsHeader` | 去头后恢复原始数据 |
| `RoundTrip` | `rx(tx(sdu)) == sdu` |
| `EmptySdu` | 空 SDU → 仅头部 |
| `PdcpRlcVertical` | PDCP tx → RLC tm_tx → RLC tm_rx → PDCP rx round-trip |

## 关键验证点

PDCP 测试中的 `PdcpRlcVertical` 是**首个垂直集成测试**，验证了 PDCP 和 RLC 两层的协同工作：

```cpp
auto pdcp_pdu = pdcp::tx(original);
auto rlc_pdu  = rlc::tm_tx(pdcp_pdu);
auto rlc_out  = rlc::tm_rx(rlc_pdu);
auto final    = pdcp::rx(rlc_out);
EXPECT_EQ(final, original);
```
