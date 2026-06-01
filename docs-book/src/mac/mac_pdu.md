# MAC PDU 编解码

位置: `stack/mac/include/mac/mac_pdu.h` / `src/mac_pdu.cpp`

## 子头格式

```
   7   6   5   4   3   2   1   0
 ┌───┬───┬───────────────────────┐
 │ R │ F │      LCID (6)        │  字节 0
 └───┴───┴───────────────────────┘

 F=0:  ┌───────────────────────┐
        │     L (8 bits)        │  字节 1
        └───────────────────────┘
        (最大长度 255)

 F=1:  ┌───────────────────────┐
        │     L_hi (8 bits)     │  字节 1
        ├───────────────────────┤
        │     L_lo (8 bits)     │  字节 2
        └───────────────────────┘
        (最大长度 65535)
```

## 特殊 LCID

| LCID | 含义 | 用途 |
|------|------|------|
| 0 | CCCH | RACH MSG3 携带的公共控制信道 |
| 1–32 | 逻辑信道 | 用户面/控制面数据 |
| 63 (0x3F) | Padding | PDU 尾部填充 |

## 接口

```cpp
namespace mac {
  constexpr uint8_t LCID_CCCH = 0;
  constexpr uint8_t LCID_PADDING = 63;

  struct MacSubheader {
    uint8_t lcid = 0;
    uint16_t length = 0;
  };

  // 构建MAC PDU: 输入多个 (lcid, sdu) 对
  std::vector<uint8_t> build_pdu(
    const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& sdus);

  // 解析MAC PDU: 输出 (lcid, sdu) 对
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>>
  parse_pdu(const std::vector<uint8_t>& pdu);
}
```

## F-bit 规则

- SDU 长度 ≤ 255: F=0, L 占 1 字节
- SDU 长度 > 255: F=1, L 占 2 字节 (大端序)

## 测试覆盖 (4 个)

| 测试 | 验证内容 |
|------|---------|
| SingleSduRoundTrip | 单 SDU 编码→解码内容不变 |
| MultipleSdus | 多 SDU (不同 LCID) 正确分离 |
| LargeSduFbit1 | >255 字节 SDU 触发 F=1 |
| PaddingSubheader | LCID=63 padding 正确解析 |
