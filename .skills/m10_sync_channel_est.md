# M10 小区搜索与同步 (PSS/SSS/DMRS)

## 目标
物理层引入真实的同步与信道估计：开机盲检小区身份、符号定时对齐、
导频均衡对抗频率选择性衰落。

## 组成

### 同步 (`stack/phy/sync`)
- PSS：Zadoff-Chu 根序列（LTE 根 25/29/34 → NID2∈{0,1,2}），
  滑动匹配滤波给出**符号定时** + NID2；归一化采用双能量
  |corr|²/(E_t·E_w) ∈[0,1]，避免高功率数据区伪峰
- SSS：第二 ZC 族（根 7/13 → NID1∈{0,1}）；PCI = 3·NID1+NID2（6 小区）
- DMRS：PCI 派生 LCG→QPSK 全栅格参考；LS 估计 H[k]=Y/X + 逐子载波均衡
- PCI 确认：SSS 与 DMRS 行归一化相关平均 ≥0.5

### 成帧 (`stack/phy/frame`)
- burst = [CP][PSS][CP][SSS][CP][DMRS][CP][data×N]；N 按载荷动态分配
- `phy_preamble_burst` / `phy_tx_data` 分离——射频适配层拼接二者，
  协议编排层不感知前导
- RX：PSS 定时 → FFT → PCI 确认 → LS 均衡 → QPSK 软判决输出

## 关键教训（调试实录）
1. **FFT 方向约定**必须全局统一（forward=exp(−jπ)，inverse=exp(+j)/N）。
   往返单测会掩盖方向反转，跨实现对比（RX 频域 vs 参考频域）立即暴露。
2. ZC 是 chirp：其 CP 相关携带确定性假斜率，CFO 估计不能用 PSS 的 CP。
3. 数据子载波 IFFT 缺失的故障模式是"幅度正确、相位随机"——
   幅度对比（|rx| vs |ref|）可快速区分"位置错"与"相位错"。

## DoD 结果 (2026-08-24)
干净/15dB SNR+偏移/两径衰落三场景：定时精确、PCI 识别正确、
均衡后误码 <1%；真实进程 attach→traffic→detach 经 AWGN+5% 丢包
信道 69 包回环 PASS。114/114 测试全绿。
