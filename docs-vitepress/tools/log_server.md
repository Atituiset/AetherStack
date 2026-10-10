# Log Server

位置: `tools/log_server/log_server.py`

## 功能

C++ 协议栈 UDP JSON 日志 → WebSocket 扇出，供 Web LMT 实时消费；
同时是 LMT→节点的**命令通道**与演示编排的事件总线。

```
C++ Logger ──UDP:9999──► log_server ──WS:8765──► LMT / demo_scenario
LMT ──WS 命令帧──► log_server ──UDP──► UE:10101 / BS:10102
demo_scenario ──UDP DEMO_PHASE──► log_server ──WS──► LMT
```

## 关键机制 (M6.5 T9)

* **背压**：每个 WS 客户端独立有界队列（512 条），慢客户端丢旧保新并
  计数告警，绝不阻塞扇出或无限占用内存
* **`_seq` 序列号**：广播消息附加服务端单调序号，LMT PDU store 按其
  增量处理，杜绝滑窗重复
* **命令通道**：入站 JSON 帧 `{"target":"ue","cmd":"attach"}` 转发为
  原始行到 `127.0.0.1:{10101|10102}`
* 任意来源的合法 JSON 日志（如 demo 的 `module=DEMO`）原样进入管道

## 使用

```bash
.venv/bin/python3 tools/log_server/log_server.py
# 监听 UDP 0.0.0.0:9999 与 WS 0.0.0.0:8765，通常由 start_demo.sh 拉起
```
