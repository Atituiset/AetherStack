# MAC 层

AetherStack 的 MAC 层实现了两个核心功能：**MAC PDU 编解码**和**4 步竞争随机接入 (RACH)**。

## 设计决策

| 参数 | 值 | 说明 |
|------|---|------|
| 子头格式 | `[R\|F\|LCID]` + 可变 L | 简化版 3GPP 38.321 |
| F-bit | 0: L=1字节, 1: L=2字节 | 支持大 SDU |
| CCCH LCID | 0 | RACH MSG3 使用 |
| Padding LCID | 63 (0x3F) | PDU 尾部填充 |
| RACH 前导码 | 42 (默认) | 可通过 `RachConfig` 配置 |
| RA-RNTI | `0x4300 \| preamble` | BS 分配 |
| C-RNTI | 递增 (0x0001 起) | BS 分配 |
| 最大 RACH 重试 | 3 | `max_preamble_transmissions` |

## 目录结构

```
stack/mac/
├── include/mac/
│   ├── mac_pdu.h      # PDU 编解码 + LCID 常量
│   ├── rach_common.h  # RACH 枚举/回调/配置
│   ├── rach_ue.h      # UE 侧 RACH 状态机
│   └── rach_bs.h      # BS 侧 RACH 处理器
├── src/
│   ├── mac_pdu.cpp
│   ├── rach_common.cpp
│   ├── rach_ue.cpp
│   └── rach_bs.cpp
└── tests/
    ├── test_mac_pdu.cpp   # 4 个 PDU 测试
    ├── test_rach_fsm.cpp  # 7 个 RACH FSM 测试
    └── test_rach_e2e.cpp  # 1 个 E2E 双端测试
```
