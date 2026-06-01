# RACH 随机接入

## 四步握手流程

```
    UE                                    BS
    │                                     │
    │──── MSG1: PRACH Preamble ──────────→│  preamble_index=42
    │                                     │  分配 RA-RNTI=0x4300|preamble
    │←─── MSG2: RAR ────────────────────│  RA-RNTI, TA, UL Grant
    │                                     │
    │──── MSG3: RRC Setup Request ──────→│  携带 RA-RNTI
    │                                     │  分配 C-RNTI
    │←─── MSG4: Contention Resolve ──────│  C-RNTI
    │                                     │
    [状态: CONNECTED]              [状态: RA Success]
```

## RACH 通用定义

位置: `stack/mac/include/mac/rach_common.h`

```cpp
namespace mac {
  using PreambleIndex = uint8_t;     // 0-63
  using RaRnti = uint16_t;

  enum class RachMsgType : uint8_t {
    MSG1_PRACH = 1,
    MSG2_RAR = 2,
    MSG3_RRC_REQ = 3,
    MSG4_CONTENTION_RESOLVE = 4,
  };

  enum class RachState : uint8_t {
    IDLE = 0,
    WAIT_RAR = 1,
    WAIT_CONTENTION_RESOLVE = 2,
    CONNECTED = 3,
  };

  using RachSendCallback  = std::function<void(RachMsgType, const std::vector<uint8_t>&)>;
  using RachStateCallback = std::function<void(RachState old_state, RachState new_state)>;

  struct RachConfig {
    PreambleIndex preamble_index = 42;
    uint16_t rar_window_ms = 10;
    uint16_t content_resolve_window_ms = 20;
    uint8_t max_preamble_transmissions = 3;
  };
}
```

## UE 侧状态机

位置: `stack/mac/include/mac/rach_ue.h`

```cpp
class RachUe {
public:
  explicit RachUe(const RachConfig& config = {});
  void set_send_callback(RachSendCallback cb);
  void set_state_callback(RachStateCallback cb);
  RachState state() const;

  void start_rach();           // 触发 RACH (RRC 调用)
  void on_rar_received(RaRnti ra_rnti, uint16_t ta, uint8_t ul_grant);
  void on_contention_resolve(uint16_t crnti);
  void on_rar_timeout();       // RAR 超时 → 重发 MSG1
  void on_contention_resolve_timeout();
};
```

### 状态转移

```
IDLE ──[start_rach]──→ WAIT_RAR ──[on_rar_received]──→ WAIT_CR ──[on_contention_resolve]──→ CONNECTED
  ↑                       │                                │
  └──[max retries]────────┘                                │
  └──[max retries]────────────────────────────────────────┘
```

**超时重试**: `on_rar_timeout()` 递增 `preamble_tx_count_` 并直接重发 MSG1 (不调用 `start_rach` 以避免重置计数器)。超过 `max_preamble_transmissions` 后转入 IDLE 并记录 `RACH_FAILED`。

## BS 侧处理器

位置: `stack/mac/include/mac/rach_bs.h`

```cpp
class RachBs {
public:
  void set_send_callback(RachSendCallback cb);
  void set_state_callback(RachStateCallback cb);

  void on_prach_received(PreambleIndex preamble_idx);  // 处理 MSG1
  void on_msg3_received(RaRnti ra_rnti,
                        const std::vector<uint8_t>& msg3_data);  // 处理 MSG3

  struct UeContext {
    RaRnti ra_rnti = 0;
    uint16_t c_rnti = 0;
    PreambleIndex preamble = 0;
    bool rach_complete = false;
  };

  const UeContext* find_ue(RaRnti ra_rnti) const;
  bool is_rach_complete(RaRnti ra_rnti) const;
};
```

### RA-RNTI 分配规则

`RA-RNTI = 0x4300 | preamble_index`

例: preamble=42 → RA-RNTI = 0x4300 | 0x2A = 0x432A = 17194

### C-RNTI 分配

从 `next_crnti_ = 0x0001` 起递增，每完成一次 RACH 分配一个。
