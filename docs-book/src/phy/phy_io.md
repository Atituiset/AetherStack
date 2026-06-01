# PHY I/O 序列化

位置: `stack/phy/include/phy/phy_io.h` / `src/phy_io.cpp`

PHY I/O 提供 IQ 采样和比特流与字节缓冲区之间的序列化，用于 UDP 传输。

## 接口

```cpp
namespace phy {
  // IQ 采样 → 字节缓冲: [uint32_t count][float re0][float im0]...
  std::vector<uint8_t> iq_to_bytes(const std::vector<std::complex<float>>& samples);

  // 字节缓冲 → IQ 采样
  std::vector<std::complex<float>> bytes_to_iq(const uint8_t* data, size_t len);

  // 比特流 → 字节缓冲: [uint32_t bit_count][uint8_t packed_bits...]
  std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits);

  // 字节缓冲 → 比特流
  std::vector<uint8_t> bytes_to_bits(const uint8_t* data, size_t len);
}
```

## 数据格式

### IQ 字节格式

```
┌────────────┬────────┬────────┬────────┬────────┐
│ count (4B) │ re₀(4B)│ im₀(4B)│ re₁(4B)│ im₁(4B)│ ...
└────────────┴────────┴────────┴────────┴────────┘
```

- `count`: uint32_t, IQ 采样对数
- 每对: float (4 bytes) real + float (4 bytes) imag
- 总字节 = 4 + count × 8

### 比特字节格式

```
┌────────────┬───────────┬───────────┐
│ bit_count  │ byte0     │ byte1     │ ...
│ (4B)       │ (8 bits)  │ (8 bits)  │
└────────────┴───────────┴───────────┘
```

- `bit_count`: uint32_t, 总比特数
- 比特按 MSB-first 打包为字节

## 使用场景

UE 和 BS 进程通过 UDP 交换 IQ 样本时使用 `iq_to_bytes` / `bytes_to_iq`。
