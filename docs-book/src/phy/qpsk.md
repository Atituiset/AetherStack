# QPSK 调制解调

位置: `stack/phy/include/phy/qpsk.h` / `src/qpsk.cpp`

## 星座映射 (Gray 编码)

```
        Q
   01 ●     ● 00
      │       │
──────┼───────┼────── I
      │       │
   11 ●     ● 10
```

| 比特对 (I,Q) | 复数符号 |
|-------------|---------|
| 00 | (+1 + 1j) / √2 |
| 01 | (-1 + 1j) / √2 |
| 10 | (+1 - 1j) / √2 |
| 11 | (-1 - 1j) / √2 |

归一化至平均功率 = 1。

## 接口

```cpp
namespace phy {
  // 调制: 2 bits → 1 complex symbol
  // bits 长度必须为偶数
  std::vector<std::complex<float>> qpsk_modulate(const std::vector<uint8_t>& bits);

  // 硬解调: 1 complex symbol → 2 bits
  // 判决: real>0 → I_bit=0, imag>0 → Q_bit=0
  std::vector<uint8_t> qpsk_demodulate(const std::vector<std::complex<float>>& symbols);
}
```

## 实现细节

- **调制**: 每 2 比特映射为一个 QPSK 符号，I_bit 控制 real 分量 (0→+1, 1→-1)，Q_bit 控制 imag 分量
- **解调**: 硬判决，以 0 为门限。`real(s) * √2 > 0` → I_bit=0，反之 I_bit=1
- **奇数比特**: 如果输入比特数为奇数，末尾补 0

## 测试覆盖 (7 个)

| 测试 | 验证内容 |
|------|---------|
| AllZeroBits | 全 0 比特 → 全 (+1+1j)/√2 |
| AllOneBits | 全 1 比特 → 全 (-1-1j)/√2 |
| SpecificPatterns | 特定 01/10 模式映射正确 |
| OddBitCount | 奇数比特自动补零 |
| AveragePower | 平均功率 ≈ 1.0 |
| RoundTripWithNoise | SNR=30dB 加噪后 BER=0 |
| PythonCrossCheck | 与 `phy_ref.py` 输出一致 |
