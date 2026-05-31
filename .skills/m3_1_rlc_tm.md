# M3.1 RLC TM 实体

## 目标
实现 RLC 透明模式（Transparent Mode）实体：SDU 直接透传为 PDU，不做任何分段、重组、ARQ 处理。这是最简单的 RLC 模式，用于传控信令和短数据。

## 范围
- `tm_tx()`: 接收上层 SDU，直接透传（零拷贝或值拷贝）
- `tm_rx()`: 接收下层 PDU，直接透传
- 无头部添加、无分段、无重组
- 接口预留 `mode` 参数，TM 是特例

## 接口契约

```cpp
namespace rlc {
// TM 模式：SDU 直接映射为 PDU，不做任何处理
std::vector<uint8_t> tm_tx(const std::vector<uint8_t>& sdu);
std::vector<uint8_t> tm_rx(const std::vector<uint8_t>& pdu);
}
```

## 验证标准
1. 任意 SDU 经 tm_tx → tm_rx round-trip 后内容不变
2. 空 SDU 输入返回空 PDU
3. 大 SDU (10KB) round-trip 正确

## 依赖
- M2 MAC PDU 编解码

## 产出文件
- `stack/rlc/include/rlc/rlc_tm.h`
- `stack/rlc/src/rlc_tm.cpp`
- `stack/rlc/tests/test_rlc_tm.cpp`
