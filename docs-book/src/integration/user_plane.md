# 用户面乒乓测试

位置: `stack/tests/test_user_plane.cpp` (4 个测试)

验证用户面数据通过完整 PHY 栈 (含 AWGN) 的端到端乒乓传输。

## 测试详解

### UserPlane.PingPongSmallPayload

小载荷 (16 字节) 经 UE→BS→UE 往返:

```
UE App.send_data(16B)
  → PDCP tx → RLC tm → MAC build → PHY tx → AWGN(30dB)
  → PHY rx → MAC parse → RLC tm → PDCP rx
  → BS App.on_data_received()

BS App.send_data(response)
  → ... 同样路径 ...
  → UE App.on_data_received()
```

验证: 双方 `tx_count=1, rx_count=1`, 数据内容一致。

### UserPlane.PingPongLargePayload

512 字节大载荷乒乓，验证大帧 OFDM 分块正确。

### UserPlane.MultiplePings

连续 3 次乒乓，验证计数器正确递增 (`tx_count=3, rx_count=3`)。

### UserPlane.BidirectionalData

UE 和 BS 同时发送数据 (半双工模拟)，验证互不干扰。

## 关键实现细节

- 使用 `bytes_to_bits()` / `bits_to_bytes()` 在 App 层和 PHY 层之间转换
- `phy_rx(samples, n_data_bits)` 中 `n_data_bits` 必须与原始数据比特数匹配
- AWGN 固定种子 (gen=42) 保证测试可重复
- `add_awgn()` 辅助函数与 `test_vertical.cpp` 中完全相同 (代码重复)
