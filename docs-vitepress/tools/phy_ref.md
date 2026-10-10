# PHY 参考模型

位置: `tools/ref_models/phy_ref.py`

## 功能

使用 NumPy 实现的 PHY 层参考模型，用于与 C++ 实现交叉验证。

## 依赖

```bash
pip install numpy
```

## 提供的函数

```python
import numpy as np

def bits_to_symbols_qpsk(bits: np.ndarray) -> np.ndarray:
    """QPSK 调制 (Gray 编码, 归一化 1/√2)"""
    # 00 → (+1+1j)/√2, 01 → (-1+1j)/√2
    # 10 → (+1-1j)/√2, 11 → (-1-1j)/√2

def symbols_to_bits_qpsk(symbols: np.ndarray) -> np.ndarray:
    """QPSK 硬解调"""

def ofdm_tx(symbols: np.ndarray, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """OFDM 发端 (numpy.fft.ifft + CP 插入)"""

def ofdm_rx(samples: np.ndarray, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """OFDM 收端 (去 CP + numpy.fft.fft)"""

def add_awgn(signal: np.ndarray, snr_db: float) -> np.ndarray:
    """AWGN 信道"""

def phy_tx(bits: np.ndarray, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """完整 PHY 发链: bits → QPSK → OFDM"""

def phy_rx(samples: np.ndarray, n_data_bits: int,
           n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """完整 PHY 收链: OFDM → QPSK → bits"""
```

## 交叉验证

C++ 测试 `QpskModem.PythonCrossCheck` 将 C++ `qpsk_modulate` 输出与此模型的 `bits_to_symbols_qpsk` 输出逐符号对比，确保两者一致。

## 与 C++ 实现的差异

| 方面 | C++ | Python |
|------|-----|--------|
| FFT | 自研 Cooley-Tukey | `numpy.fft.ifft/fft` |
| 精度 | float (单精度) | float64 (双精度) |
| 复数类型 | `std::complex<float>` | `np.complex128` |
| 归一化 | `1/sqrt(2)` 宏 | `1/np.sqrt(2)` |
