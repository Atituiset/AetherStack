# M11 MAC 调度器 + 多 UE

## 目标
单小区支持多个 UE 并发附着与流量：下行轮询调度、上行免调度接入、
每流独立 HARQ、共享介质下的正确寻址。

## 设计

### 流表 (`stack/core/bs_node.h` DlFlow)
```
flows_: map<C-RNTI, {HarqTx dl, HarqRx ul, deque<TB> queue, stats}>
```
每个已连接 UE 一条流：独立的下行 HARQ 发送实体、上行 HARQ 接收实体
与调度队列。UE 释放（NAS DETACH）时整条流销毁。

### 调度语义
| 方向 | 机制 | 与 3GPP 对应 |
|------|------|-------------|
| 下行 | 每 tick 每流至多提交一个新 TB（公平全遍历）；HARQ 重传优先于新块 | DCI 调度的简化：轮询替代 CQI/优先级判决 |
| 上行 | 免调度（configured grant）：UE 任意时刻可发，BS 无需授权 | ConfiguredGrantConfig 的极端形式 |

### 共享介质正确性（UE 侧）
* DATA 帧 RNTI 过滤：非本机单播直接丢弃
* MSG2 仅在 WAIT_RAR 态消费；MSG4 仅在 WAIT_CONTENTION_RESOLVE 态消费
  ——否则邻 UE 的竞争解决会污染本机 C-RNTI 缓存

## DoD 结果 (2026-08-24)
双 UeNode 交错附着（第二 UE 在第一 UE 已注册后加入）→ 各自 5 包并发
回环零丢失零串扰 → C-RNTI 唯一、流表计数=2 → UE1 分离后 UE2 流量不受影响。
115/115 测试常规 + ASan 双构建全绿。
