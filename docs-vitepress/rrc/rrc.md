# RRC 无线资源控制层

RRC 层负责 UE 与 BS 之间的连接管理，包括广播 (MIB/SIB1) 和连接建立/释放。

## 设计决策

| 参数 | 值 | 说明 |
|------|---|------|
| 消息格式 | TLV: [1B msg_type][2B length][value] | 简化版 3GPP 38.331 |
| UE 状态 | IDLE → CONNECTING → CONNECTED | 三状态 FSM |
| C-RNTI 分配 | BS 侧递增 (0x0001 起) | 与 RACH C-RNTI 一致 |
| MIB 字段 | sfn, dl_bandwidth, phich_config | 最小广播信息 |
| SIB1 字段 | plmn_id, tac, cell_id | 最小系统信息 |

## 目录结构

```
stack/rrc/
├── include/rrc/
│   ├── rrc_types.h      # UeState, RrcMessageType 枚举
│   ├── rrc_messages.h   # Mib, Sib1, RrcMessage 编解码
│   ├── rrc_ue.h         # UE 侧 RRC 实体
│   └── rrc_bs.h         # BS 侧 RRC 实体
├── src/
│   ├── rrc_types.cpp
│   ├── rrc_messages.cpp
│   ├── rrc_ue.cpp
│   └── rrc_bs.cpp
└── tests/
    └── test_rrc.cpp     # 10 个测试
```

## RRC 连接流程

```
    UE                                   BS
    │                                    │
    │←─── MIB Broadcast ────────────────│  (周期性广播)
    │←─── SIB1 Broadcast ───────────────│  (周期性广播)
    │                                    │
    │──── RRC Setup Request ───────────→│  UE 发起连接
    │     [msg_type=1]                   │
    │                                    │  分配 C-RNTI
    │←─── RRC Setup ────────────────────│  [msg_type=2, crnti]
    │                                    │
    │──── RRC Setup Complete ──────────→│  [msg_type=3]
    │                                    │
    [状态: CONNECTED]             [状态: CONNECTED]
```
