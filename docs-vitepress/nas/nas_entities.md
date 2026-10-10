# NAS 实体

## NasUe (UE 侧)

位置: `stack/nas/include/nas/nas_ue.h`

### 接口

```cpp
class NasUe {
public:
  using SendCallback = std::function<void(const std::vector<uint8_t>&)>;

  void set_send_callback(SendCallback cb);
  nas::UeState state() const;
  const std::string& imsi() const;
  uint32_t assigned_tmsi() const;

  void send_attach_request(const std::string& imsi);
  void on_message(const std::vector<uint8_t>& pdu);
};
```

### 状态转移

```
DEREGISTERED ──[send_attach_request]──→ REGISTERING ──[收到 Attach Accept]──→ REGISTERED
```

- `send_attach_request(imsi)`: 存储 IMSI, 发送 ATTACH_REQUEST, 状态 → REGISTERING
- `on_message(ATTACH_ACCEPT)`: 提取 TMSI, 状态 → REGISTERED

---

## NasBs (BS 侧)

位置: `stack/nas/include/nas/nas_bs.h`

### 接口

```cpp
class NasBs {
public:
  using SendCallback = std::function<void(uint32_t tmsi, const std::vector<uint8_t>&)>;

  void set_send_callback(SendCallback cb);
  void handle_message(uint32_t tmsi, const std::vector<uint8_t>& pdu);
  bool is_ue_registered(uint32_t tmsi) const;

  struct UeContext {
    std::string imsi;
    uint32_t tmsi = 0;
    bool registered = false;
  };
  const UeContext* find_ue(uint32_t tmsi) const;
};
```

### 行为

- `handle_message(ATTACH_REQUEST)`: 创建 `UeContext`, 存 IMSI, 分配 TMSI (`next_tmsi_++`), 回复 `ATTACH_ACCEPT`
- `next_tmsi_` 从 `0x00010001` 起递增
- TMSI 用作 BS→UE 回调的路由键

### 注意: 命名空间

`nas::UeState` 与 `rrc::UeState` 是**不同枚举**，跨层使用需完全限定。
