# PHY 物理层

AetherStack 的 PHY 层实现了 5G NR 最小物理层子集：QPSK 调制/解调 + OFDM 收发。不依赖外部 FFT 库，使用自研 Cooley-Tukey 原地 FFT。

## 设计决策

| 参数 | 值 | 说明 |
|------|---|------|
| 调制 | QPSK (Gray 编码) | 固定，不支持自适应 |
| IFFT 点数 | 64 | `DEFAULT_N_FFT` |
| CP 长度 | 16 | `DEFAULT_CP_LEN` |
| 归一化 | 1/√2 ≈ 0.7071 | `QPSK_NORM`，平均功率=1 |
| FFT 算法 | Cooley-Tukey 原位 | 无外部依赖 |
| 天线配置 | SISO 1T1R | 无 MIMO |

## PHY 常量

```cpp
// stack/phy/include/phy/phy_common.h
namespace phy {
  constexpr int DEFAULT_N_FFT = 64;
  constexpr int DEFAULT_CP_LEN = 16;
  constexpr double QPSK_NORM = 0.7071067811865476;  // 1/sqrt(2)
}
```

## 目录结构

```
stack/phy/
├── include/phy/
│   ├── phy_common.h    # 常量定义
│   ├── qpsk.h          # QPSK 调制/解调
│   ├── ofdm.h          # OFDM 收发 + phy_tx/rx
│   └── phy_io.h        # IQ/比特 序列化
├── src/
│   ├── qpsk.cpp
│   ├── ofdm.cpp
│   └── phy_io.cpp
└── tests/
    ├── test_qpsk.cpp   # 7 个 QPSK 测试
    └── test_ofdm.cpp   # 8 个 OFDM/Tx/Rx 测试
```
