# M1.2+M1.3 OFDM 发端/收端

## 目标
实现 OFDM 调制（IFFT + CP 插入）与解调（CP 移除 + FFT），构成完整的 OFDM 物理层收发链路。Cooley-Tukey 原地 FFT，不依赖外部库。

## 范围
- IFFT：频域符号 → 时域采样
- FFT：时域采样 → 频域符号
- CP：插入/移除循环前缀
- 完整 PHY 链路：`phy_tx(bits) → IQ samples`，`phy_rx(IQ samples) → bits`

## 接口契约

```cpp
namespace phy {
struct OfdmConfig {
    size_t ifft_size = 64;
    size_t cp_len = 16;
    size_t data_subcarriers = 48;  // 中间 48 个子载波承载用户数据
};

// OFDM 发端：bits → IQ 采样
std::vector<complex> ofdm_tx(const std::vector<uint8_t>& bits, const OfdmConfig& cfg = {});
std::vector<complex> phy_tx(const std::vector<uint8_t>& bits, const OfdmConfig& cfg = {});

// OFDM 收端：IQ 采样 → bits
std::vector<uint8_t> ofdm_rx(const std::vector<complex>& samples, const OfdmConfig& cfg = {});
std::vector<uint8_t> phy_rx(const std::vector<complex>& samples, const OfdmConfig& cfg = {});

// Cooley-Tukey FFT（原地）
void fft(std::vector<complex>& x, bool inverse);
}
```

## 关键参数
- IFFT size = 64, CP = 16, 每个 OFDM 符号长度 = 80
- 数据子载波：索引 1~48（DC 不承载）
- 每 OFDM 符号承载 48 个 QPSK 符号 = 96 bits

## 验证标准
1. `ofdm_tx` 输出长度 = N_symbols × (ifft_size + cp_len)
2. CP 内容 = 时域尾部 cp_len 个采样的拷贝
3. 无噪声 round-trip: 原始 bits == 恢复 bits
4. 低噪声 round-trip: 误码率 < 1e-3
5. `phy_tx → phy_rx` 全链路通过
6. Python 参考模型输出一致

## 依赖
- M1.1 QPSK 调制/解调

## 产出文件
- `stack/phy/include/phy/ofdm.h`
- `stack/phy/src/ofdm.cpp`
- `stack/phy/tests/test_ofdm.cpp`
