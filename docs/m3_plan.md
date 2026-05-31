# M3 计划书：RLC / PDCP 透传

> **状态**: `COMPLETED` ✅ — RLC TM + PDCP 透传 + 全链路垂直测试，43 个单测全通过。
> **预计工期**: 1~2 周
> **前置依赖**: M2 MAC 层完成（RACH 可用）

---

## 目标

建立 **RLC 透明模式（TM）** 和 **PDCP 透传实体**，完成用户面/控制面数据的垂直贯通测试。这是第一个跨越多层的集成验证点。

**核心约束**：
- RLC：仅 TM 模式（无分段/重组、无 ARQ）
- PDCP：无头压缩（ROHC）、无加密/完整性保护、无按序递交
- 目标：数据包从 PDCP 顶到底再回顶，内容不变

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M3.1** | RLC TM 实体 | `rlc_tm.md` | 单元测试：输入任意 SDU，输出 PDU 与之相同 |
| **M3.2** | PDCP 透传实体 | `pdcp_transparent.md` | 单元测试：同上；完成 PDCP↔RLC 垂直集成测试 |
| **M3.3** | 全链路垂直测试 | `vertical_passthrough.md` | 一个 IP 包穿过 PDCP→RLC→MAC→L1→信道→L1→MAC→RLC→PDCP，两端内容一致 |

---

## 接口契约

### PDCP ↔ RLC

```cpp
namespace pdcp {
    // 接收来自上层的 SDU（RRC 或 IP 包），添加 PDCP 头（简化：无头或固定占位头）
    std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu);

    // 从 RLC 接收 PDU，去掉 PDCP 头后递交上层
    std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu);
}
```

### RLC ↔ MAC

```cpp
namespace rlc {
    // TM 模式：SDU 直接映射为 PDU，不做任何处理
    std::vector<uint8_t> tm_tx(const std::vector<uint8_t>& sdu);
    std::vector<uint8_t> tm_rx(const std::vector<uint8_t>& pdu);
}
```

### 垂直数据流

```
Upper Layer (RRC/IP)
    ↓ SDU
[PDCP]  tx() → PDCP PDU
    ↓
[RLC]   tm_tx() → RLC PDU (same as SDU in TM)
    ↓
[MAC]   build_pdu() → MAC PDU
    ↓
[PHY]   tx() → IQ samples
    ↓
[Channel Sim]
    ↓
[PHY]   rx() → MAC PDU
    ↓
[MAC]   parse_pdu() → RLC SDU
    ↓
[RLC]   tm_rx() → PDCP PDU
    ↓
[PDCP]  rx() → Upper Layer SDU
```

---

## 目录结构（M3 新增）

```
stack/
├── rlc/
│   ├── CMakeLists.txt
│   ├── include/rlc/
│   │   └── rlc_tm.h
│   ├── src/
│   │   └── rlc_tm.cpp
│   └── tests/
│       └── test_rlc_tm.cpp
├── pdcp/
│   ├── CMakeLists.txt
│   ├── include/pdcp/
│   │   └── pdcp_entity.h
│   ├── src/
│   │   └── pdcp_entity.cpp
│   └── tests/
│       └── test_pdcp_entity.cpp
```

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 层间接口耦合 | 后续加 AM/UM 时需要重构 | 接口设计预留 `mode` 参数，TM 是特例 |
| 数据拷贝开销 | 大量 std::vector 拷贝 | 先用值语义保证正确性，M7 优化时再引入移动语义/共享缓冲区 |
| 头部格式预留 | 当前无头，后续加 ROHC 时格式会变 | 在 PDCP 头中预留 version 字段 |

---

## 技能卡片清单

```
.skills/
├── m3_1_rlc_tm.md
├── m3_2_pdcp_transparent.md
└── m3_3_vertical_passthrough.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M3.1 RLC TM | 🟢 完成 | 4 个 RLC TM 单测通过 |
| M3.2 PDCP 透传 | 🟢 完成 | 5 个 PDCP 单测通过（含 PDCP↔RLC 垂直集成） |
| M3.3 垂直测试 | 🟢 完成 | 4 个全链路垂直测试通过（PDCP→RLC→MAC→PHY→AWGN→PHY→MAC→RLC→PDCP） |

---

## 更新日志

- `2026-05-31`: 初始计划书创建
- `2026-06-01`: M3.1 RLC TM 完成（tm_tx/tm_rx 透传）
- `2026-06-01`: M3.2 PDCP 透传完成（2 字节简化头 + PDCP↔RLC 垂直集成）
- `2026-06-01`: M3.3 全链路垂直测试通过（PDCP→RLC→MAC→PHY→AWGN→反向回传）
