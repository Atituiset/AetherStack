# 集成测试

AetherStack 包含三类集成测试，从局部到全栈逐步验证协议栈各层的协同工作。

| 类型 | 文件 | 测试数 | 覆盖范围 |
|------|------|--------|---------|
| 垂直直通 | `test_vertical.cpp` | 4 | PDCP↔RLC↔MAC↔PHY 无线链路 |
| 完整附着 | `test_full_attach.cpp` | 2 | 冷启动→SIB→RACH→RRC→NAS |
| 用户面乒乓 | `test_user_plane.cpp` | 4 | App→PDCP→RLC→MAC→PHY 往返 |

## 辅助函数

`test_vertical.cpp` 和 `test_user_plane.cpp` 中定义了相同的辅助函数:

```cpp
std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits);
std::vector<std::complex<float>> add_awgn(
    const std::vector<std::complex<float>>& samples, double snr_db);
```

- AWGN 使用固定种子 `std::mt19937 gen(42)` 保证可重复
- `bytes_to_bits` / `bits_to_bytes`: 字节↔比特流转换
