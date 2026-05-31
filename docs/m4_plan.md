# M4 计划书：RRC 与 NAS 最小功能

> **状态**: `PLANNED` — 尚未开始实现。本文档随实现进度持续更新。
> **预计工期**: 2~3 周
> **前置依赖**: M3 RLC/PDCP 透传完成
>
> 🎯 **本阶段完成后，UE 可完成从开机到网络注册的完整流程。**

---

## 目标

实现 **RRC 连接建立/释放** 和 **NAS 简化附着流程**，将 M1~M3 的各层能力串联为完整的控制面流程。这是 MVP 的**核心验证点**——第一次能观察到 UE 从冷启动到 `"Attach Complete"` 的全过程。

**核心约束**：
- RRC：最简系统消息（MIB/SIB1 骨架），只广播必要参数
- NAS：不做完整鉴权（AKA 流程省略），只做模拟 IMSI 交换
- 安全：无加密/完整性保护（留到 V2）

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M4.1** | RRC 系统消息 | `rrc_sib.md` | BS 周期性广播精简 MIB；UE 接收并解析，日志打印内容 |
| **M4.2** | RRC 连接建立 | `rrc_connection.md` | 双端状态机进入 `RRC_CONNECTED`；日志 MSC 显示 3 条消息交互 |
| **M4.3** | NAS 简化附着 | `nas_attach.md` | UE 发送 Attach Request（IMSI），BS 返回 Attach Accept；NAS 状态变为 `REGISTERED` |
| **M4.4** | 初始附着全流程 | `full_attach.md` | **一键启动**：UE+BS+信道，冷启动 → 小区搜索 → SIB → RACH → RRC → NAS → `"Attach Complete"` |

---

## 接口契约

### RRC ↔ PDCP

```cpp
namespace rrc {
    enum class State { IDLE, CONNECTING, CONNECTED };

    // UE 侧：发起 RRC 连接建立请求
    void send_setup_request();

    // BS 侧：处理 RRC 消息，维护 UE 上下文
    void handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu);

    // 获取当前状态（用于日志和 LMT 显示）
    State get_state() const;
}
```

### NAS ↔ RRC

```cpp
namespace nas {
    enum class State { DEREGISTERED, REGISTERING, REGISTERED };

    // UE 侧：发送 Attach Request
    void send_attach_request(const std::string& imsi);

    // BS 侧：处理 NAS 消息
    void handle_message(uint16_t tmsi, const std::vector<uint8_t>& pdu);

    State get_state() const;
}
```

---

## 完整附着流程（M4.4 目标）

```
[UE 开机]
   ↓
[PHY] 小区搜索 / 同步（M1）
   ↓
[PHY] 接收 MIB（M4.1）
   ↓
[PHY] 接收 SIB1（M4.1）
   ↓
[MAC] RACH MSG1~4（M2）
   ↓
[RRC] RRC Setup Request → RRC Setup → RRC Setup Complete（M4.2）
   ↓  [状态: RRC_CONNECTED]
[NAS] Attach Request → Attach Accept（M4.3）
   ↓  [状态: REGISTERED]
[应用] 数据传输就绪
```

Web LMT 应显示：
- UE 状态：`OFFLINE` → `INITIALIZING` → `RUNNING` → `REGISTERED`
- MSC 图：完整的消息序列

---

## 目录结构（M4 新增）

```
stack/
├── rrc/
│   ├── CMakeLists.txt
│   ├── include/rrc/
│   │   ├── rrc_ue.h          # UE 侧 RRC 实体
│   │   ├── rrc_bs.h          # BS 侧 RRC 实体
│   │   ├── rrc_messages.h    # RRC 消息格式（简化 ASN.1）
│   │   └── rrc_types.h       # 状态枚举、常量
│   ├── src/
│   │   ├── rrc_ue.cpp
│   │   ├── rrc_bs.cpp
│   │   └── rrc_messages.cpp
│   └── tests/
│       ├── test_rrc_state.cpp
│       └── test_rrc_connection.cpp
├── nas/
│   ├── CMakeLists.txt
│   ├── include/nas/
│   │   ├── nas_ue.h
│   │   ├── nas_bs.h
│   │   └── nas_messages.h
│   ├── src/
│   │   ├── nas_ue.cpp
│   │   ├── nas_bs.cpp
│   │   └── nas_messages.cpp
│   └── tests/
│       └── test_nas_attach.cpp
```

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| RRC 状态机复杂 | Agent 容易遗漏异常路径 | 先用 YAML DSL 定义，生成代码 |
| NAS 消息格式 | 3GPP 24.301 消息复杂 | 只用 TLV 子集，不实现完整编解码器 |
| 层间耦合 | 修改某层可能影响全流程 | 每完成一个小里程碑就做一次垂直回归测试 |
| 时序依赖 | SIB 周期、RACH 窗口需要帧定时 | 简化：用消息驱动代替真实时间驱动 |

---

## 技能卡片清单

```
.skills/
├── m4_1_rrc_sib.md
├── m4_2_rrc_connection.md
├── m4_3_nas_attach.md
└── m4_4_full_attach.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M4.1 RRC SIB | 🔴 未开始 | 前置：M3 完成 |
| M4.2 RRC 连接 | 🔴 未开始 | |
| M4.3 NAS 附着 | 🔴 未开始 | |
| M4.4 全流程 | 🔴 未开始 | |

---

## 更新日志

- `2026-05-31`: 初始计划书创建
