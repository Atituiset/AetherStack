# PDCP 透传实体

位置: `stack/pdcp/include/pdcp/pdcp_entity.h` / `src/pdcp_entity.cpp`

## PDCP 头部格式

```
  字节 0: [ version(4b) | reserved(4b) ]
  字节 1: [ seq_num (8b) ]
```

- `version = 0` (当前唯一版本)
- `seq_num`: 占位符，暂不用于排序/重传
- 总头部大小: `PDCP_HEADER_SIZE = 2`

## 接口

```cpp
namespace pdcp {
  constexpr uint8_t PDCP_HEADER_SIZE = 2;

  // PDCP 发送: 添加 2 字节头
  // PDU = [header(2)] + [SDU]
  std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu);

  // PDCP 接收: 去除 2 字节头
  // SDU = PDU[2:]
  std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu);
}
```

## 尺寸关系

```
SDU 大小 = N
PDCP PDU 大小 = N + 2  (添加头部)
经 RLC TM 后大小不变 = N + 2
经 MAC PDU 后大小 > N + 2  (添加 MAC 子头)
```

## 测试覆盖 (5 个)

| 测试 | 验证内容 |
|------|---------|
| `TxAddsHeader` | PDU 大小 = SDU 大小 + 2, version=0 |
| `RxStripsHeader` | 解头后恢复原始 SDU |
| `RoundTrip` | `rx(tx(sdu)) == sdu` |
| `EmptySdu` | 空 SDU → 2 字节头 PDU |
| `PdcpRlcVertical` | PDCP→RLC→RLC→PDCP 垂直集成 round-trip |
