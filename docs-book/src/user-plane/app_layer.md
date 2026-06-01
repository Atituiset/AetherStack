# App Layer 用户面

位置: `stack/app/include/app/app_layer.h`

## 功能

AppLayer 是用户面最顶层，提供简单的数据发送/接收接口，用于端到端数据乒乓测试。

## 接口

```cpp
namespace app {
  class AppLayer {
  public:
    using SendCallback = std::function<void(const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);
    void send_data(const std::vector<uint8_t>& data);
    void on_data_received(const std::vector<uint8_t>& data);

    uint32_t tx_count() const;
    uint32_t rx_count() const;
    const std::vector<uint8_t>& last_received() const;
  };
}
```

## 行为

- `send_data()`: 递增 `tx_count_`, 通过回调下发数据 (→ PDCP tx → RLC tm → MAC build_pdu → PHY)
- `on_data_received()`: 递增 `rx_count_`, 存储到 `last_rx_` (← PHY → MAC → RLC tm → PDCP rx)
- 日志记录: 每次收发均通过 `LOG_INFO` 记录事件 `APP_TX` / `APP_RX` 及字节数

## 典型使用 (User Plane Ping-Pong)

```cpp
app::AppLayer ue_app, bs_app;

// UE 发送
ue_app.send_data(payload);    // tx_count=1
  → PDCP tx → RLC tm → MAC → PHY → ... → PHY → MAC → RLC tm → PDCP rx →
bs_app.on_data_received(data);  // rx_count=1

// BS 回复
bs_app.send_data(response);   // tx_count=1
  → ... →
ue_app.on_data_received(data);  // rx_count=1
```

## 测试覆盖

4 个 user_plane 测试 + 3 个 stack 测试验证 AppLayer 计数和内容正确性。
