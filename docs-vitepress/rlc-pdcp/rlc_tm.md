# RLC 透明模式

位置: `stack/rlc/include/rlc/rlc_tm.h` / `src/rlc_tm.cpp`

## 接口

```cpp
namespace rlc {
  // TM 发送: SDU 直接映射为 PDU，不做任何处理
  std::vector<uint8_t> tm_tx(const std::vector<uint8_t>& sdu);

  // TM 接收: PDU 直接映射为 SDU，不做任何处理
  std::vector<uint8_t> tm_rx(const std::vector<uint8_t>& pdu);
}
```

## 实现说明

RLC TM 是纯粹的透传层：

- `tm_tx(sdu)` 返回 `sdu` 本身 (值拷贝)
- `tm_rx(pdu)` 返回 `pdu` 本身 (值拷贝)
- 无头部添加、无分段、无重组、无 ARQ

该层的存在是为了保持 3GPP 分层架构的完整性。当后续需要支持 UM/AM 模式时，只需在此层添加对应逻辑，而不影响上下层接口。

## 测试覆盖 (4 个)

| 测试 | 验证内容 |
|------|---------|
| `TmTxReturnsInput` | `tm_tx(sdu) == sdu` |
| `TmRxReturnsInput` | `tm_rx(pdu) == pdu` |
| `RoundTrip` | `tm_rx(tm_tx(data)) == data` |
| `EmptySdu` | 空 SDU 正确透传 |
