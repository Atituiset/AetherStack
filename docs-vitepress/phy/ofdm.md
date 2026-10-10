# OFDM 收发

位置: `stack/phy/include/phy/ofdm.h` / `src/ofdm.cpp`

## OFDM 符号结构

```
┌──CP──┬──────── IFFT 输出 ────────┐
│ 16s  │        64 samples          │  = 1 OFDM 符号 (80 samples)
└──────┴───────────────────────────┘
```

- CP = 循环前缀，取 IFFT 输出的最后 `cp_len` 个样本
- 总符号长度 = `n_fft + cp_len` = 80 samples (默认)

## 接口

```cpp
namespace phy {
  // OFDM 发端: 频域符号 → 时域采样 (含 CP)
  std::vector<std::complex<float>> ofdm_tx(
    const std::vector<std::complex<float>>& symbols,
    int n_fft = 64, int cp_len = 16);

  // OFDM 收端: 时域采样 → 频域符号 (去 CP + FFT)
  std::vector<std::complex<float>> ofdm_rx(
    const std::vector<std::complex<float>>& samples,
    int n_fft = 64, int cp_len = 16);

  // 完整 PHY 发链: bits → QPSK → OFDM → 时域
  std::vector<std::complex<float>> phy_tx(
    const std::vector<uint8_t>& bits,
    int n_fft = 64, int cp_len = 16);

  // 完整 PHY 收链: 时域 → OFDM 解调 → QPSK 解调 → bits
  // n_data_bits: 期望输出比特数 (用于确定符号数)
  std::vector<uint8_t> phy_rx(
    const std::vector<std::complex<float>>& samples,
    size_t n_data_bits,
    int n_fft = 64, int cp_len = 16);
}
```

## 实现细节

### Cooley-Tukey FFT

自研原地 FFT，无外部依赖：

```cpp
// 蝶形运算，位反转排序
void fft_in_place(std::vector<std::complex<float>>& x, bool inverse);
```

- 时间复杂度: O(N log N)
- 输入自动零填充到 2 的幂次
- IFFT: 先 FFT，再取共轭，除以 N

### ofdm_tx 流程

1. 将频域符号按 `n_fft` 分块
2. 对每块做 IFFT 得到时域
3. 复制最后 `cp_len` 个样本作为 CP
4. 拼接: `[CP | IFFT output]`

### ofdm_rx 流程

1. 按 `n_fft + cp_len` 分块
2. 丢弃每块前 `cp_len` 个样本 (CP)
3. 对剩余 `n_fft` 个样本做 FFT
4. 拼接所有频域符号

### phy_tx / phy_rx

- `phy_tx`: bits → `qpsk_modulate` → 零填充至 `n_fft` 整数倍 → `ofdm_tx`
- `phy_rx`: `ofdm_rx` → 取前 `n_data_bits/2` 个符号 → `qpsk_demodulate`

**关键**: `phy_rx` 需要 `n_data_bits` 参数来确定解调的符号数量。
