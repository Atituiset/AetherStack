# M2 计划书：MAC 层与随机接入

> **状态**: `IN_PROGRESS` — M2.1 + M2.2 + M2.3 + M2.4 完成，12 个 MAC 单测全通过。
> **预计工期**: 3~5 周
> **前置依赖**: M1 物理层闭环（PHY Tx/Rx 可用）

---

## 目标

在 M1 提供的物理层 `bitstream` 传输能力之上，实现 **MAC 层最小功能集**：
- MAC PDU 的组包/拆包
- 基于竞争的随机接入过程（RACH，4 步握手）
- UE 侧和 BS 侧的状态机

**核心约束**：
- 调度器：最简单的轮询（Round Robin），不做复杂 PF 调度
- HARQ：暂不实现（或只做单次重传骨架）
- 逻辑信道优先级：固定顺序，不做动态优先级

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M2.1** | MAC PDU 编解码 | `mac_pdu.md` | 单元测试：任意 SDU 编码 → 解码后内容不变 |
| **M2.2** | RACH 状态机引擎 | `mac_rach_fsm.md` | YAML/DSL 定义状态机，生成 C++ 代码；单测覆盖所有状态转换 |
| **M2.3** | RACH UE 侧 | `mac_rach_ue.md` | 与 BS 桩联测：触发后收到 RAR，并发送 MSG3 |
| **M2.4** | RACH BS 侧 | `mac_rach_bs.md` | **双端真联**：UE 和 BS 跑完四步 RACH，BS 日志显示 `"RA Success"` |

---

## 接口契约

### MAC ↔ RLC 层接口

```cpp
namespace mac {
    // 从 RLC 接收 SDU，封装为 MAC PDU
    // input:  rlc_sdu (std::vector<uint8_t>), logical_channel_id
    // output: mac_pdu (std::vector<uint8_t>)
    std::vector<uint8_t> build_pdu(const std::vector<uint8_t>& rlc_sdu, uint8_t lcid);

    // 从 PHY 接收 MAC PDU，解封装为 RLC SDU
    // input:  mac_pdu (从 PHY rx 获得的 bitstream 解析而来)
    // output: 多个 (lcid, rlc_sdu) 对
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> parse_pdu(const std::vector<uint8_t>& mac_pdu);
}
```

### MAC ↔ PHY 层接口

```cpp
namespace mac {
    // MAC 层将 MAC PDU 交给 PHY 发送
    // 内部调用 phy::tx(mac_pdu_bits)
    void send_to_phy(const std::vector<uint8_t>& mac_pdu);

    // PHY 收到数据后回调 MAC
    // 由 PHY 层在 rx() 成功后调用
    void on_phy_rx(const std::vector<uint8_t>& mac_pdu);
}
```

### RACH 状态机（UE 侧）

```
IDLE --(RRC 触发)--> WAIT_RAR --(收到 RAR)--> WAIT_CR --(收到竞争解决)--> CONNECTED
                         ↑                    |
                         └---(超时/冲突)-------┘
```

---

## RACH 四步流程（简化版）

```
UE                          BS
 |                           |
 | --- MSG1: PRACH --------> |  UE 发送前导码（preamble_index）
 |                           |
 | <--- MSG2: RAR ---------- |  BS 回复：RA-RNTI, Timing Advance
 |                           |
 | --- MSG3: RRC_SETUP_REQ-> |  UE 发送 RRC 建立请求（含临时 ID）
 |                           |
 | <--- MSG4: 竞争解决 ----- |  BS 发送竞争解决标识
 |                           |
 [状态: CONNECTED]           [状态: RA Success]
```

**M2 阶段的 MSG3/MSG4 内容可以简化**：
- MSG3 不携带真实的 RRC 消息，只放占位数据
- MSG4 只确认竞争解决，不触发真实的 RRC 状态变更（留到 M4 实现）

---

## 目录结构（M2 新增）

```
stack/
├── mac/
│   ├── CMakeLists.txt
│   ├── include/mac/
│   │   ├── mac_pdu.h         # MAC PDU 编解码
│   │   ├── rach_ue.h         # UE 侧 RACH 状态机
│   │   ├── rach_bs.h         # BS 侧 RACH 处理
│   │   └── rach_common.h     # RACH 常量（前导码格式、时频资源）
│   ├── src/
│   │   ├── mac_pdu.cpp
│   │   ├── rach_ue.cpp
│   │   ├── rach_bs.cpp
│   │   └── rach_common.cpp
│   └── tests/
│       ├── test_mac_pdu.cpp
│       ├── test_rach_fsm.cpp
│       └── test_rach_e2e.cpp  # UE+BS 联调测试
```

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 状态机时序复杂 | Agent 生成代码容易遗漏超时/回退路径 | 先用 YAML 状态机 DSL 定义全部转移，再生成代码 |
| MSG2 RAR 窗口定时 | 需要与 PHY 帧定时对齐 | 简化：用消息计数代替真实时间，PHY 每收到一个 preamble 立即回 RAR |
| 冲突检测 | 两个 UE 同时接入 | M2 阶段只测单 UE 接入，多 UE 冲突留到 M5+ |
| MAC PDU 格式 | 3GPP 38.321 子头格式复杂 | 先用固定长度子头，不支持可变长度 |

---

## 技能卡片清单

```
.skills/
├── m2_1_mac_pdu.md
├── m2_2_rach_fsm.md
├── m2_3_rach_ue.md
├── m2_4_rach_bs.md
└── m2_5_rach_e2e_test.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M2.1 MAC PDU | 🟢 完成 | 4 个 PDU 编解码单测通过 |
| M2.2 RACH FSM | 🟢 完成 | RachState enum + 状态转换回调 |
| M2.3 RACH UE | 🟢 完成 | 5 个 UE 单测（含超时重试） |
| M2.4 RACH BS | 🟢 完成 | 2 个 BS 单测 + 1 个 E2E 双端联调 |

---

## 更新日志

- `2026-05-31`: 初始计划书创建
- `2026-05-31`: M2.1 MAC PDU 编解码完成（build_pdu/parse_pdu，F-bit 长度字段）
- `2026-05-31`: M2.2-M2.4 RACH 状态机完成（UE 4步握手、BS RAR/CR 响应、E2E 联调 12 单测全过）
