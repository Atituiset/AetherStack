# 分析脚本

## generate_msc.py

位置: `tools/scripts/generate_msc.py`

将 JSON 日志文件转换为 Mermaid 序列图:

```bash
python tools/scripts/generate_msc.py < log.json > sequence.mmd
```

输出为 Mermaid `sequenceDiagram` 语法，可直接在 Markdown 中渲染。

事件映射:

| 日志事件 | 序列图消息 |
|---------|-----------|
| MAC_RACH_MSG1 | UE → BS: PRACH Preamble |
| MAC_RACH_MSG2 | BS → UE: RAR |
| MAC_RACH_MSG3 | UE → BS: RRC Setup Request |
| MAC_RACH_MSG4 | BS → UE: Contention Resolve |
| RRC_SETUP_REQUEST_TX | UE → BS: RRC Setup Request |
| RRC_SETUP_TX | BS → UE: RRC Setup |
| RRC_SETUP_COMPLETE_TX | UE → BS: RRC Setup Complete |
| NAS_ATTACH_REQUEST | UE → BS: Attach Request |
| NAS_ATTACH_ACCEPT_TX | BS → UE: Attach Accept |

---

## pdu_analyzer.py

位置: `tools/scripts/pdu_analyzer.py`

十六进制 PDU 逐层解码:

```bash
python tools/scripts/pdu_analyzer.py --hex "3c0a..." --layer mac
```

支持层: `mac`, `rlc`, `pdcp`, `rrc`, `nas`

输出每层头部字段和载荷内容。

---

## latency_report.py

位置: `tools/scripts/latency_report.py`

从 JSON 日志计算 RTT 统计:

```bash
python tools/scripts/latency_report.py < log.json
```

输出:

```
Total messages: 42
RTT samples:    21
Min RTT:        0.5 ms
Max RTT:        3.2 ms
Mean RTT:       1.4 ms
Std RTT:        0.6 ms
P50:            1.2 ms
P95:            2.8 ms
P99:            3.1 ms
```

匹配逻辑: UE TX 事件 ↔ BS RX 事件时间差。
