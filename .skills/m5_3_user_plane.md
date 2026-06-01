# M5.3 用户面数据收发

## 目标
在 NAS 注册完成后，UE 发送应用数据（如 "Hello"），BS 回复（如 "World"），验证用户面数据全栈贯通。

## 范围
- 应用层接口：UE/BS 各有一个 AppLayer 类
- 数据路径：App → PDCP → RLC → MAC → PHY → AWGN → PHY → MAC → RLC → PDCP → App
- Ping-pong 测试：UE 发 "Hello"，BS 回 "World"
- RTT 计算：UE 记录发送时间戳，收到回复后计算 RTT

## 接口契约

```cpp
namespace app {
class AppLayer {
public:
    void set_send_callback(std::function<void(const std::vector<uint8_t>&)> cb);
    void on_data_received(const std::vector<uint8_t>& data);
    void send_data(const std::vector<uint8_t>& data);
};
}
```

## 验证标准
1. UE 发送 "Hello"，BS 接收并回复 "World"
2. UE 收到 "World"，内容正确
3. 全栈 PHY+AWGN round-trip: 数据内容一致
4. RTT 打印到控制台

## 依赖
- M4 全流程
- M3 垂直透传

## 产出文件
- `stack/app/include/app/app_layer.h`
- `stack/app/src/app_layer.cpp`
- `stack/tests/test_user_plane.cpp`
