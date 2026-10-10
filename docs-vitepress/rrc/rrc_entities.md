# RRC 实体

## RrcUe (UE 侧)

位置: `stack/rrc/include/rrc/rrc_ue.h`

### 接口

```cpp
class RrcUe {
public:
  using SendCallback = std::function<void(const std::vector<uint8_t>&)>;

  void set_send_callback(SendCallback cb);
  rrc::UeState state() const;
  uint16_t assigned_crnti() const;

  void on_mib_received(const Mib& mib);    // 接收广播 MIB
  void on_sib1_received(const Sib1& sib1); // 接收广播 SIB1
  void start_connection();                  // 发起 RRC 连接
  void on_message(const std::vector<uint8_t>& pdu);  // 接收 BS 消息
};
```

### 状态转移

```
IDLE ──[start_connection]──→ CONNECTING ──[收到 Setup]──→ CONNECTED
  │                                │
  │     [收到 SetupComplete 确认]   │
  └────────────────────────────────┘
```

- `on_mib_received()`: 存储但不改变状态
- `on_sib1_received()`: 存储但不改变状态
- `start_connection()`: 发送 `SETUP_REQUEST`, 状态 → CONNECTING
- `on_message()` with SETUP: 提取 C-RNTI, 发送 `SETUP_COMPLETE`, 状态 → CONNECTED
- `on_message()` with RELEASE: 状态 → IDLE

---

## RrcBs (BS 侧)

位置: `stack/rrc/include/rrc/rrc_bs.h`

### 接口

```cpp
class RrcBs {
public:
  using SendCallback = std::function<void(uint16_t rnti, const std::vector<uint8_t>&)>;

  void set_send_callback(SendCallback cb);
  void handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu);
  bool is_ue_connected(uint16_t rnti) const;

  Mib broadcast_mib() const;
  Sib1 broadcast_sib1() const;

  struct UeContext {
    uint16_t c_rnti = 0;
    UeState state = UeState::IDLE;
  };
  const UeContext* find_ue(uint16_t rnti) const;
};
```

### 行为

- `handle_message(SETUP_REQUEST)`: 创建 `UeContext`, 分配 C-RNTI, 回复 `SETUP`
- `handle_message(SETUP_COMPLETE)`: 更新 UeContext 状态为 CONNECTED
- `broadcast_mib()` / `broadcast_sib1()`: 返回默认广播信息
- `next_crnti_` 从 0x0001 递增

### 注意: 命名空间冲突

`rrc::UeState` 和 `nas::UeState` 是不同枚举。跨层引用时必须使用完全限定名:

```cpp
rrc::UeState rrc_state = rrc_ue.state();
nas::UeState nas_state = nas_ue.state();
```
