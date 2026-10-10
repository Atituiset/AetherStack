# PHY 测试

## 测试套件概览 (15 个测试)

| 套件 | 文件 | 测试数 | 覆盖范围 |
|------|------|--------|---------|
| QpskModem | `test_qpsk.cpp` | 7 | 星座映射, 功率, 噪声 round-trip |
| OfdmTransceiver | `test_ofdm.cpp` | 8 | FFT, CP, OFDM round-trip, E2E |

## QPSK 测试详解

| 测试名 | 验证内容 |
|--------|---------|
| `AllZeroBits` | `00` → `(+1+1j)/√2` |
| `AllOneBits` | `11` → `(-1-1j)/√2` |
| `SpecificPatterns` | `01` → `(-1+1j)/√2`, `10` → `(+1-1j)/√2` |
| `OddBitCount` | 5 比特 → 补零到 6 比特再调制 |
| `AveragePower` | `E[|s|²] ≈ 1.0` |
| `RoundTripWithNoise` | SNR=30dB AWGN 后 BER=0 |
| `PythonCrossCheck` | 与 `phy_ref.py` `bits_to_symbols_qpsk` 输出逐符号对比 |

## OFDM 测试详解

| 测试名 | 验证内容 |
|--------|---------|
| `FftRoundTrip` | FFT → IFFT 恢复原始信号 |
| `CpInsertion` | CP = IFFT 输出最后 cp_len 个样本 |
| `OfdmRoundTrip` | `ofdm_tx` → `ofdm_rx` → 频域符号一致 |
| `MultipleOfdmSymbols` | 多符号 OFDM 正确分块 |
| `OfdmWithNoise` | SNR=30dB 后 EVM 可接受 |
| `PhyTxRx` | `phy_tx` → AWGN(30dB) → `phy_rx` BER=0 |
| `PhyTxRxLargerPayload` | 512 比特载荷 round-trip |
| `PhyTxRxOddBits` | 奇数比特自动补零 |

## AWGN 辅助函数

测试中使用固定种子 `std::mt19937 gen(42)` 确保可重复性：

```cpp
std::vector<std::complex<float>> add_awgn(
    const std::vector<std::complex<float>>& samples,
    double snr_db) {
  double signal_power = /* 平均功率 */;
  double sigma = std::sqrt(signal_power / (2 * snr_linear));
  std::normal_distribution<float> dist(0.0f, sigma);
  // 逐采样加噪声
}
```
