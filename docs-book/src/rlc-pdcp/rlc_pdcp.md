# RLC / PDCP 层

RLC 和 PDCP 是 AetherStack 中最简化的两层，均采用透传模式。RLC TM 不做任何处理，PDCP 仅添加 2 字节简化头。

## 设计决策

| 层 | 模式 | 头部大小 | 不实现的功能 |
|----|------|---------|-------------|
| RLC | 透明模式 (TM) | 0 | 分段/重组, ARQ, 状态报告, UM, AM |
| PDCP | 透传 | 2 字节 | ROHC, 加密, 完整性保护, 按序递交, 重复检测 |

## 数据流

```
上层 SDU
  │
  ├── PDCP tx(): 添加 2 字节头
  │   PDCP PDU = [version(4b)|reserved(4b)][seq_num(8b)] + SDU
  │   PDU 大小 = SDU 大小 + 2
  │
  ├── RLC tm_tx(): 直接透传
  │   RLC PDU = PDCP PDU (完全相同)
  │
  └── 传递给 MAC build_pdu()
```

## 目录结构

```
stack/rlc/
├── include/rlc/
│   └── rlc_tm.h
├── src/
│   └── rlc_tm.cpp
└── tests/
    └── test_rlc_tm.cpp    # 4 个测试

stack/pdcp/
├── include/pdcp/
│   └── pdcp_entity.h
├── src/
│   └── pdcp_entity.cpp
└── tests/
    └── test_pdcp_entity.cpp   # 5 个测试
```
