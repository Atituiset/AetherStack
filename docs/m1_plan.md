# M1 计划书：物理层最小闭环（Phase 1 PHY）

> **状态**: `PLANNED` — 尚未开始实现。本文档随实现进度持续更新。
> **预计工期**: 2~4 周
> **前置依赖**: M0 骨架（CMake/Logger/Channel Sim/Web LMT）

---

## 目标

建立 UE 与 BS 之间**最简物理层数据通路**：UE 侧将比特流调制为 OFDM 时域采样，经仿真信道（AWGN）传输，BS 侧解调恢复原始比特流。为后续 MAC/RLC/PDCP/RRC/NAS 提供透明的 `bitstream` 传输服务。

**核心约束**：
- SISO（单天线），无需 MIMO
- 固定调制：QPSK（暂不引入 16QAM/64QAM）
- 信道：AWGN（加性高斯白噪声），Python 仿真实现
- 同步：简化处理（理想定时同步或滑动相关粗同步）

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M1.1** | QPSK 调制/解调 | `phy_qpsk.md` | Python 参考模型与 C++ 实现输出互相关峰值 > 0.999；单元测试通过 |
| **M1.2** | OFDM 发端（Tx） | `phy_ofdm_tx.md` | 复数符号 → 时域 OFDM 符号（含 CP）；与 Python 参考模型输出完全一致（浮点误差容忍） |
| **M1.3** | OFDM 收端（Rx） | `phy_ofdm_rx.md` | 时域采样 → 频域符号；经 AWGN 后 EVM < 阈值；含简化定时同步 |
| **M1.4** | L1 双端闭环 | `phy_e2e_test.md` | UE 发端 ↔ 仿真信道 ↔ BS 收端：发送已知序列，接收端误码率为 0（高 SNR 下） |

---

## 接口契约

### L1 ↔ MAC 层接口（上下行对称）

```cpp
// 上行：MAC → L1
namespace phy {
    // 发送一个传输块（Transport Block）
    // input:  bitstream (std::vector<uint8_t>)
    // output: IQ 采样序列 (std::vector<std::complex<float>>)
    std::vector<std::complex<float>> tx(const std::vector<uint8_t>& bits);

    // 下行：L1 → MAC
    // input:  IQ 采样序列 (从信道接收)
    // output: 解调后的 bitstream
    // 返回 empty 表示解调失败（CRC 错误 / 同步失败）
    std::vector<uint8_t> rx(const std::vector<std::complex<float>>& iq_samples);
}
```

### L1 ↔ 信道仿真接口

```cpp
// UE 侧：phy_tx() 输出的 IQ 采样 → UDP 发送到信道
// BS 侧：从信道 UDP 接收 IQ 采样 → phy_rx() 解调
// IQ 采样序列通过 UDP 以 float[2] 数组（I, Q 交替）传输
```

---

## 目录结构（M1 新增）

```
stack/
├── phy/
│   ├── CMakeLists.txt
│   ├── include/phy/
│   │   ├── qpsk.h          # QPSK 调制/解调
│   │   ├── ofdm.h          # OFDM Tx/Rx
│   │   └── phy_common.h    # 物理层公共常量（子载波数、CP 长度等）
│   ├── src/
│   │   ├── qpsk.cpp
│   │   ├── ofdm.cpp
│   │   └── phy_common.cpp
│   └── tests/
│       ├── test_qpsk.cpp   # 对比 Python 参考模型
│       └── test_ofdm.cpp   # 收发一致性测试
```

---

## Python 参考模型

`tools/ref_models/phy_ref.py`：
- `qpsk_modulate(bits)` → 复数符号
- `qpsk_demodulate(symbols)` → 比特流
- `ofdm_tx(symbols, n_fft, cp_len)` → 时域采样
- `ofdm_rx(samples, n_fft, cp_len)` → 频域符号
- `awgn_channel(iq_samples, snr_db)` → 加噪采样

C++ 实现必须与参考模型输出逐 sample 一致（浮点误差 < 1e-4）。

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| QPSK 软解调 vs 硬解调 | 影响后续链路自适应 | 先用硬解调，M1.4 验证通过后再考虑软信息 |
| 定时同步简化导致误码 | M1.4 误码率不为 0 | 先假设理想同步（已知定时偏移），M2 再引入真实同步 |
| FFT 实现选择 | 性能 vs 复杂度 | 用 kiss FFT（轻量）或 std::complex 手动实现小型 FFT |
| 浮点精度差异 | C++ vs Python 输出不一致 | 统一使用 `float`（非 double），允许相对误差 |

---

## 技能卡片清单

```
.skills/
├── m1_1_qpsk.md              # QPSK 调制/解调
├── m1_2_ofdm_tx.md           # OFDM 发端
├── m1_3_ofdm_rx.md           # OFDM 收端 + 简化同步
├── m1_4_phy_e2e_test.md      # L1 双端闭环集成测试
└── m1_ref_python_model.md    # Python 参考模型规范
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M1.1 QPSK | ✅ 完成 | 7 个单元测试通过，Python 参考模型验证 BER=0 |
| M1.2 OFDM Tx | 🔴 未开始 | |
| M1.3 OFDM Rx | 🔴 未开始 | |
| M1.4 E2E 闭环 | 🔴 未开始 | |

---

## 更新日志

- `2026-06-01`: M1.1 QPSK 完成 — C++ 实现 + Python 参考模型 + 7 个 Google Test 用例
- `2026-05-31`: 初始计划书创建
