# API 参考

## 完整命名空间索引

### `phy` — 物理层

| 函数 | 签名 | 说明 |
|------|------|------|
| `qpsk_modulate` | `std::vector<std::complex<float>>(const std::vector<uint8_t>& bits)` | QPSK 调制 |
| `qpsk_demodulate` | `std::vector<uint8_t>(const std::vector<std::complex<float>>& symbols)` | QPSK 解调 |
| `ofdm_tx` | `std::vector<std::complex<float>>(const std::vector<std::complex<float>>& symbols, int n_fft=64, int cp_len=16)` | OFDM 发端 |
| `ofdm_rx` | `std::vector<std::complex<float>>(const std::vector<std::complex<float>>& samples, int n_fft=64, int cp_len=16)` | OFDM 收端 |
| `phy_tx` | `std::vector<std::complex<float>>(const std::vector<uint8_t>& bits, int n_fft=64, int cp_len=16)` | 完整 PHY 发链 |
| `phy_rx` | `std::vector<uint8_t>(const std::vector<std::complex<float>>& samples, size_t n_data_bits, int n_fft=64, int cp_len=16)` | 完整 PHY 收链 |
| `iq_to_bytes` | `std::vector<uint8_t>(const std::vector<std::complex<float>>& samples)` | IQ→字节 |
| `bytes_to_iq` | `std::vector<std::complex<float>>(const uint8_t*, size_t)` | 字节→IQ |
| `bits_to_bytes` | `std::vector<uint8_t>(const std::vector<uint8_t>& bits)` | 比特→字节 |
| `bytes_to_bits` | `std::vector<uint8_t>(const uint8_t*, size_t)` | 字节→比特 |

### `mac` — MAC 层

| 函数/类 | 签名 | 说明 |
|---------|------|------|
| `build_pdu` | `std::vector<uint8_t>(const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>&)` | 构建 MAC PDU |
| `parse_pdu` | `std::vector<...>(const std::vector<uint8_t>&)` | 解析 MAC PDU |
| `RachUe` | 类: `start_rach()`, `on_rar_received()`, `on_contention_resolve()`, `on_rar_timeout()` | UE RACH FSM |
| `RachBs` | 类: `on_prach_received()`, `on_msg3_received()` | BS RACH 处理器 |

### `rlc` — RLC 层

| 函数 | 签名 | 说明 |
|------|------|------|
| `tm_tx` | `std::vector<uint8_t>(const std::vector<uint8_t>& sdu)` | TM 发送 (透传) |
| `tm_rx` | `std::vector<uint8_t>(const std::vector<uint8_t>& pdu)` | TM 接收 (透传) |

### `pdcp` — PDCP 层

| 函数 | 签名 | 说明 |
|------|------|------|
| `tx` | `std::vector<uint8_t>(const std::vector<uint8_t>& sdu)` | PDCP 发送 (+2B 头) |
| `rx` | `std::vector<uint8_t>(const std::vector<uint8_t>& pdu)` | PDCP 接收 (-2B 头) |

### `rrc` — RRC 层

| 类/函数 | 说明 |
|---------|------|
| `RrcUe` | `set_send_callback()`, `on_mib_received()`, `on_sib1_received()`, `start_connection()`, `on_message()`, `state()`, `assigned_crnti()` |
| `RrcBs` | `set_send_callback()`, `handle_message()`, `is_ue_connected()`, `broadcast_mib()`, `broadcast_sib1()`, `find_ue()` |
| `Mib` | `encode()`, `decode()`, 字段: `sfn`, `dl_bandwidth`, `phich_config` |
| `Sib1` | `encode()`, `decode()`, 字段: `plmn_id`, `tac`, `cell_id` |
| `RrcMessage` | `encode()`, `decode()`, 字段: `msg_type`, `value` |
| `generate_mib(sfn)` | 生成 MIB |
| `generate_sib1()` | 生成默认 SIB1 |

### `nas` — NAS 层

| 类/函数 | 说明 |
|---------|------|
| `NasUe` | `set_send_callback()`, `send_attach_request()`, `on_message()`, `state()`, `imsi()`, `assigned_tmsi()` |
| `NasBs` | `set_send_callback()`, `handle_message()`, `is_ue_registered()`, `find_ue()` |
| `NasMessage` | `encode()`, `decode()`, 字段: `msg_type`, `value` |

### `app` — 用户面

| 类 | 说明 |
|----|------|
| `AppLayer` | `set_send_callback()`, `send_data()`, `on_data_received()`, `tx_count()`, `rx_count()`, `last_received()` |

### `logging` — 日志

| 函数/宏 | 说明 |
|---------|------|
| `init(module, host, port)` | 初始化日志 (可选远程) |
| `log(level, event, fields)` | 输出 JSON 日志行 |
| `LOG_DEBUG/INFO/WARN/ERROR` | 便利宏 |

### `transport` — UDP 传输

| 类 | 说明 |
|----|------|
| `UdpTransport` | `bind()`, `set_dest()`, `send()`, `recv(timeout)`, `fd()` |
