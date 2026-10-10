# NAS 消息格式

位置: `stack/nas/include/nas/nas_messages.h`

## 消息类型枚举

```cpp
enum class NasMessageType : uint8_t {
  ATTACH_REQUEST = 1,   // UE → BS
  ATTACH_ACCEPT  = 2,   // BS → UE
  ATTACH_REJECT  = 3,   // BS → UE (预留)
  DETACH         = 4,   // 双向 (预留)
};
```

## TLV 编码格式

与 RRC 消息格式完全一致:

```
┌──────────────┬──────────────┬──────────────┐
│ msg_type (1B)│ length (2B)  │  value (NB)  │
│  uint8_t     │  uint16_t BE │              │
└──────────────┴──────────────┴──────────────┘
```

## NasMessage 结构

```cpp
struct NasMessage {
  NasMessageType msg_type;
  std::vector<uint8_t> value;  // 载荷

  std::vector<uint8_t> encode() const;
  static NasMessage decode(const std::vector<uint8_t>& data);
};
```

## Attach Request 编码

`value` 内容 = IMSI 的 ASCII 字节:

```
IMSI = "460010123456789"
value = [0x34, 0x36, 0x30, 0x30, 0x31, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39]
length = 15
```

## Attach Accept 编码

`value` 内容 = TMSI 的 4 字节大端序:

```
TMSI = 0x00010001
value = [0x00, 0x01, 0x00, 0x01]
length = 4
```
