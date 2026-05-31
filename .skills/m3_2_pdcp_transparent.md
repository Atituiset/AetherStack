# M3.2 PDCP 透传实体

## 目标
实现 PDCP 透传实体：SDU 经 PDCP 层添加固定占位头后透传，RX 端去掉头后递交上层。无 ROHC 头压缩、无加密/完整性保护、无按序递交。

## 范围
- `tx()`: 添加 PDCP 头（简化：version=0 + 1 字节序号占位）
- `rx()`: 去掉 PDCP 头后返回 SDU
- PDCP↔RLC 垂直集成：PDCP PDU 作为 RLC SDU 下传

## 接口契约

```cpp
namespace pdcp {
// 透传模式：添加简化 PDCP 头后下发
// 头格式：[version(4bit) | reserved(4bit)] [seq_num_lo]
// version=0, seq_num 固定递增（M3 阶段不校验）
std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu);

// 透传模式：去掉 PDCP 头后上递
std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu);
}
```

## PDCP 头格式（简化）
```
Byte 0: [version=0 | reserved=0]
Byte 1: [seq_num_lo] (M3 不使用，仅占位)
Total header: 2 bytes
```

## 验证标准
1. 任意 SDU 经 tx → rx round-trip 后内容不变
2. PDCP PDU 长度 = SDU 长度 + 2（头部）
3. 空 SDU 经 tx 后仅含 2 字节头
4. PDCP→RLC 垂直集成：PDCP PDU 作为 RLC tm_tx 输入，RLC tm_rx 输出再经 pdcp::rx 恢复原始 SDU

## 依赖
- M3.1 RLC TM 实体

## 产出文件
- `stack/pdcp/include/pdcp/pdcp_entity.h`
- `stack/pdcp/src/pdcp_entity.cpp`
- `stack/pdcp/tests/test_pdcp_entity.cpp`
