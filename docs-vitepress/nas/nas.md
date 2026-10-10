# NAS 非接入层

NAS 层负责 UE 的注册/附着管理，位于 RRC 之上，通过 RRC 消息承载。

## 设计决策

| 参数 | 值 | 说明 |
|------|---|------|
| 消息格式 | TLV: [1B msg_type][2B length][value] | 与 RRC 格式一致 |
| UE 状态 | DEREGISTERED → REGISTERING → REGISTERED | 三状态 FSM |
| IMSI | ASCII 字符串 (如 "460010123456789") | 15 位数字 |
| TMSI | 4 字节 uint32_t | BS 分配, 从 0x00010001 起 |
| TMSI 分配 | 递增 | 每次附着分配新 TMSI |

## 目录结构

```
stack/nas/
├── include/nas/
│   ├── nas_messages.h   # NasMessage TLV 编解码
│   ├── nas_ue.h         # NasUe 附着 FSM
│   └── nas_bs.h         # NasBs 处理器
├── src/
│   ├── nas_messages.cpp
│   ├── nas_ue.cpp
│   └── nas_bs.cpp
└── tests/
    └── test_nas.cpp     # 4 个测试
```

## NAS 附着流程

```
    UE                                    BS
    │                                     │
    │──── Attach Request ───────────────→│  携带 IMSI
    │     [msg_type=1, imsi]             │  查找/创建 UE 上下文
    │                                     │  分配 TMSI
    │←─── Attach Accept ─────────────────│  携带 TMSI
    │     [msg_type=2, tmsi]             │
    │                                     │
    [状态: REGISTERED]            [状态: REGISTERED]
```
