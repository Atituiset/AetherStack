# RRC 消息格式

位置: `stack/rrc/include/rrc/rrc_messages.h`

## 消息类型枚举

```cpp
enum class RrcMessageType : uint8_t {
  SETUP_REQUEST  = 1,   // UE → BS
  SETUP          = 2,   // BS → UE (含 C-RNTI)
  SETUP_COMPLETE = 3,   // UE → BS
  RELEASE        = 4,   // BS → UE
  MIB_BROADCAST  = 10,  // BS 广播
  SIB1_BROADCAST = 11,  // BS 广播
};
```

## TLV 编码格式

```
┌──────────────┬──────────────┬──────────────┐
│ msg_type (1B)│ length (2B)  │  value (NB)  │
│  uint8_t     │  uint16_t BE │              │
└──────────────┴──────────────┴──────────────┘
```

- `length` = `value` 的字节数 (大端序)
- 总消息大小 = 3 + length

## MIB 编码

```cpp
struct Mib {
  uint16_t sfn = 0;           // 系统帧号
  uint8_t  dl_bandwidth = 50; // 下行带宽 (RB 数)
  uint8_t  phich_config = 0;  // PHICH 配置 (占位)
};
```

编码: `[sfn_lo][sfn_hi][dl_bandwidth][phich_config]` (4 字节)

## SIB1 编码

```cpp
struct Sib1 {
  std::string plmn_id = "46001";  // PLMN 标识 (ASCII)
  uint16_t tac = 1;               // 跟踪区码
  uint16_t cell_id = 1;           // 小区标识
};
```

编码: `[plmn_len][plmn_bytes...][tac_lo][tac_hi][cell_id_lo][cell_id_hi]`

**重要**: 解码时必须先 `clear()` plmn_id 以避免双重前缀错误。

## RrcMessage

```cpp
struct RrcMessage {
  RrcMessageType msg_type;
  std::vector<uint8_t> value;  // 载荷 (如 Setup 中的 C-RNTI)
};

// TLV 编码
std::vector<uint8_t> encode() const;    // → [type(1)][len(2)][value(N)]
static RrcMessage decode(const std::vector<uint8_t>& data);  // 反向解析
```

## 辅助函数

```cpp
Mib  generate_mib(uint16_t sfn);   // 生成指定 SFN 的 MIB
Sib1 generate_sib1();               // 生成默认 SIB1 (plmn="46001", tac=1, cell_id=1)
```
