# M1.1 QPSK 调制/解调

## 目标
实现 QPSK 调制器与解调器，将比特流映射为复数符号（Gray 编码），反之亦然。提供 Python 参考模型交叉验证。

## 范围
- 调制：比特流 → 复数符号，Gray 编码映射
- 解调：复数符号 → 比特流，硬判决
- 归一化：单位平均功率 (1/√2)
- Python 参考模型：`tools/ref_models/phy_ref.py` 中 `bits_to_symbols_qpsk` / `symbols_to_bits_qpsk`

## 接口契约

```cpp
namespace phy {
// QPSK 调制：bit pairs → complex symbols (Gray coded)
std::vector<complex> modulate_qpsk(const std::vector<uint8_t>& bits);

// QPSK 解调：complex symbols → bit pairs (hard decision)
std::vector<uint8_t> demodulate_qpsk(const std::vector<complex>& symbols);
}
```

## 映射表（Gray 码）
| Bits | Symbol           |
|------|------------------|
| 00   | (+1+1j)/√2       |
| 01   | (+1-1j)/√2       |
| 11   | (-1-1j)/√2       |
| 10   | (-1+1j)/√2       |

## 验证标准
1. 奇数比特返回空
2. 全零比特映射到 (+1+1j)/√2
3. 无噪声 round-trip 误码率 = 0
4. 低噪声 (SNR=20dB) round-trip 误码率 < 1e-3
5. 平均功率 ≈ 1.0
6. Python 参考模型输出互相关 > 0.999

## 依赖
- M0.1 项目结构
- M0.2 统一日志

## 产出文件
- `stack/phy/include/phy/qpsk.h`
- `stack/phy/src/qpsk.cpp`
- `stack/phy/tests/test_qpsk.cpp`
- `tools/ref_models/phy_ref.py`（QPSK 部分）
