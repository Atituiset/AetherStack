# 演示运行

## 完整演示步骤

### 1. 构建 C++ 协议栈

```bash
make
```

### 2. 启动 Log Server

```bash
pip install websockets
python tools/log_server/log_server.py
```

监听: UDP:9999 → WebSocket:8765

### 3. 启动 Web LMT

```bash
cd lmt && npm install && npm run dev
```

浏览器打开: `http://localhost:5173`

### 4. 启动 BS 端

```bash
# Terminal 1: BS PHY 监听
./build/bin/bs_phy 20002 10001
```

### 5. 启动 UE 端

```bash
# Terminal 2: UE PHY 连接 BS
./build/bin/ue_phy 10001 20002
```

### 6. 观察结果

在 LMT 中观察:
- **TopologyCanvas**: UE 和 BS 之间的连接建立动画
- **FsmViewer**: MAC/RRC/NAS 状态机逐步跳转
- **MscDiagram**: 消息序列逐步出现
- **LogStream**: 实时 JSON 日志流

### 7. 可选: 信道模拟

```bash
# 在 UE 和 BS 之间插入信道模拟器
python tools/channel/sim_channel.py \
  --listen-port 30001 --dest-port 20002 --loss-rate 0.05 --delay-ms 2

# UE 连接到信道模拟器而非 BS
./build/bin/ue_phy 10001 30001
```

## 事后分析

```bash
# 生成 Mermaid 序列图
python tools/scripts/generate_msc.py < log.json > sequence.mmd

# PDU 逐层解码
python tools/scripts/pdu_analyzer.py --hex "3c0a..." --layer mac

# RTT 延迟统计
python tools/scripts/latency_report.py < log.json
```
