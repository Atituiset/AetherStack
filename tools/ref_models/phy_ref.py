#!/usr/bin/env python3
"""
AetherStack PHY Reference Model (Python Golden Reference)

Provides QPSK modulation/demodulation, OFDM Tx/Rx, and AWGN channel.
C++ implementation must match these outputs (within float tolerance).
"""

import numpy as np
from typing import List, Tuple


def bits_to_symbols_qpsk(bits: np.ndarray) -> np.ndarray:
    """QPSK modulation: 2 bits -> 1 complex symbol.
    
    Mapping (3GPP-like, Gray coded):
        00 -> +1+1j (normalized: 1/sqrt(2))
        01 -> -1+1j
        10 -> +1-1j
        11 -> -1-1j
    
    Args:
        bits: 1D array of 0/1 values, length must be even
    Returns:
        1D complex array of QPSK symbols, normalized to unit average power
    """
    if len(bits) % 2 != 0:
        raise ValueError("Number of bits must be even for QPSK")
    n_symbols = len(bits) // 2
    bits_reshaped = bits.reshape(n_symbols, 2)
    i_bits = bits_reshaped[:, 0]
    q_bits = bits_reshaped[:, 1]
    i_vals = 1 - 2 * i_bits   # 0->+1, 1->-1
    q_vals = 1 - 2 * q_bits   # 0->+1, 1->-1
    symbols = (i_vals + 1j * q_vals) / np.sqrt(2)
    return symbols


def symbols_to_bits_qpsk(symbols: np.ndarray) -> np.ndarray:
    """QPSK hard demodulation: 1 complex symbol -> 2 bits.
    
    Decision boundaries on I and Q axes (0 threshold).
    Inverse of the mapping in bits_to_symbols_qpsk.
    
    Args:
        symbols: 1D complex array of QPSK symbols
    Returns:
        1D array of 0/1 bits, length = 2 * len(symbols)
    """
    denorm = symbols * np.sqrt(2)
    i_bits = (denorm.real < 0).astype(int)   # +1->0, -1->1
    q_bits = (denorm.imag < 0).astype(int)   # +1->0, -1->1
    bits = np.stack([i_bits, q_bits], axis=1).flatten()
    return bits


def ofdm_tx(symbols: np.ndarray, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """OFDM transmit: frequency-domain symbols -> time-domain IQ samples with CP.
    
    Subcarrier mapping: DC + positive + negative (standard LTE/NR style).
    For n_fft=64: subcarriers 0..n_fft/2-1 map to IFFT bins 0..n_fft/2-1,
                  subcarriers n_fft/2..n_fft-1 map to IFFT bins -n_fft/2..-1.
    
    Args:
        symbols: complex array of frequency-domain symbols (length <= n_fft)
        n_fft: IFFT size
        cp_len: cyclic prefix length in samples
    Returns:
        1D complex array of time-domain samples (length = n_symbols * (n_fft + cp_len))
    """
    n_symbols = len(symbols) // n_fft
    if len(symbols) % n_fft != 0:
        n_symbols += 1
        pad_len = n_symbols * n_fft - len(symbols)
        symbols = np.concatenate([symbols, np.zeros(pad_len, dtype=complex)])
    
    output = np.array([], dtype=complex)
    for i in range(n_symbols):
        chunk = symbols[i * n_fft:(i + 1) * n_fft]
        time_domain = np.fft.ifft(chunk, n=n_fft)
        cp = time_domain[-cp_len:]
        ofdm_symbol = np.concatenate([cp, time_domain])
        output = np.concatenate([output, ofdm_symbol])
    
    return output


def ofdm_rx(samples: np.ndarray, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """OFDM receive: time-domain IQ samples -> frequency-domain symbols.
    
    Removes CP, applies FFT per OFDM symbol.
    
    Args:
        samples: 1D complex array of time-domain samples
        n_fft: FFT size
        cp_len: cyclic prefix length
    Returns:
        1D complex array of frequency-domain symbols
    """
    symbol_len = n_fft + cp_len
    n_symbols = len(samples) // symbol_len
    if n_symbols == 0:
        return np.array([], dtype=complex)
    
    output = np.array([], dtype=complex)
    for i in range(n_symbols):
        start = i * symbol_len + cp_len
        chunk = samples[start:start + n_fft]
        freq_domain = np.fft.fft(chunk, n=n_fft)
        output = np.concatenate([output, freq_domain])
    
    return output


def awgn_channel(iq_samples: np.ndarray, snr_db: float) -> np.ndarray:
    """Add AWGN noise to IQ samples.
    
    Args:
        iq_samples: 1D complex array
        snr_db: signal-to-noise ratio in dB
    Returns:
        noisy complex array
    """
    signal_power = np.mean(np.abs(iq_samples) ** 2)
    snr_linear = 10 ** (snr_db / 10)
    noise_power = signal_power / snr_linear
    noise = np.sqrt(noise_power / 2) * (np.random.randn(len(iq_samples)) + 1j * np.random.randn(len(iq_samples)))
    return iq_samples + noise


def phy_tx(bits: np.ndarray, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """Full PHY Tx chain: bits -> QPSK -> OFDM -> time-domain samples.
    
    Args:
        bits: 1D array of 0/1 bits
        n_fft: IFFT size
        cp_len: CP length
    Returns:
        1D complex time-domain samples
    """
    symbols = bits_to_symbols_qpsk(bits)
    n_sc = n_fft
    n_data_symbols = len(symbols)
    n_ofdm_syms = int(np.ceil(n_data_symbols / n_sc))
    padded_len = n_ofdm_syms * n_sc
    padded = np.zeros(padded_len, dtype=complex)
    padded[:n_data_symbols] = symbols
    return ofdm_tx(padded, n_fft, cp_len)


def phy_rx(samples: np.ndarray, n_data_bits: int, n_fft: int = 64, cp_len: int = 16) -> np.ndarray:
    """Full PHY Rx chain: time-domain samples -> OFDM demod -> QPSK demod -> bits.
    
    Args:
        samples: 1D complex time-domain samples
        n_data_bits: expected number of output bits
        n_fft: FFT size
        cp_len: CP length
    Returns:
        1D array of 0/1 bits
    """
    freq = ofdm_rx(samples, n_fft, cp_len)
    n_data_symbols = n_data_bits // 2
    data_symbols = freq[:n_data_symbols]
    return symbols_to_bits_qpsk(data_symbols)


if __name__ == "__main__":
    np.random.seed(42)
    bits = np.random.randint(0, 2, 128)
    print(f"Input bits:  {bits[:20]}...")
    
    symbols = bits_to_symbols_qpsk(bits)
    print(f"QPSK symbols: {symbols[:5]}")
    print(f"Avg power: {np.mean(np.abs(symbols)**2):.4f} (expect ~1.0)")
    
    recovered = symbols_to_bits_qpsk(symbols)
    ber = np.mean(bits != recovered)
    print(f"BER (no noise): {ber} (expect 0.0)")
    
    tx_samples = phy_tx(bits)
    print(f"OFDM Tx samples: {len(tx_samples)}, first 5: {tx_samples[:5]}")
    
    rx_bits = phy_rx(tx_samples, len(bits))
    ber_ofdm = np.mean(bits != rx_bits)
    print(f"BER (OFDM no noise): {ber_ofdm} (expect 0.0)")
    
    noisy = awgn_channel(tx_samples, 20.0)
    rx_bits_noisy = phy_rx(noisy, len(bits))
    ber_noisy = np.mean(bits != rx_bits_noisy)
    print(f"BER (SNR=20dB): {ber_noisy:.6f}")
