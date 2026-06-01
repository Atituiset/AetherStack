# Log Server

位置: `tools/log_server/log_server.py`

## 功能

将 C++ 协议栈通过 UDP 发送的 JSON 日志桥接到 WebSocket，供 Web LMT 实时消费。

## 架构

```
C++ Logger ──UDP:9999──→ log_server.py ──WS:8765──→ 浏览器 LMT
```

## 使用方法

```bash
# 安装依赖
pip install websockets

# 启动
python tools/log_server/log_server.py

# 默认监听
#   UDP: 0.0.0.0:9999
#   WebSocket: 0.0.0.0:8765
```

## 工作原理

1. 创建 UDP socket 绑定到 port 9999
2. 创建 WebSocket server 监听 port 8765
3. 异步循环:
   - 收到 UDP 数据报 → 解析为 JSON → 广播给所有 WebSocket 客户端
   - 新 WebSocket 连接 → 加入客户端列表
   - WebSocket 断开 → 移除客户端

## 关键实现

```python
import asyncio
import websockets

UDP_HOST = "0.0.0.0"
UDP_PORT = 9999
WS_HOST = "0.0.0.0"
WS_PORT = 8765

clients = set()

async def udp_receiver():
    loop = asyncio.get_event_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: asyncio.DatagramProtocol(),
        local_addr=(UDP_HOST, UDP_PORT))
    # 收到数据 → 广播

async def ws_handler(websocket):
    clients.add(websocket)
    try:
        await websocket.wait_closed()
    finally:
        clients.discard(websocket)
```
