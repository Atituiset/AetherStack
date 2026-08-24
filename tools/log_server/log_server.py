#!/usr/bin/env python3
"""
AetherStack Log Server (M6.5 T9)

Receives structured JSON logs via UDP from C++ protocol stack processes,
tags each with a monotonic `_seq`, and fans out to Web LMT clients via
WebSocket.

* Backpressure: every WS client gets a bounded send queue; a slow client
  has its oldest entries dropped (with a drop counter) instead of blocking
  the fan-out or growing memory unboundedly.
* Command channel: JSON text frames sent by LMT clients of the shape
  {"target": "ue"|"bs", "cmd": "<line>"} are forwarded as raw UDP lines to
  the node command ports (UE 10101 / BS 10102).
"""

import asyncio
import json
import sys

import websockets

UDP_PORT = 9999
WS_PORT = 8765
CMD_PORTS = {"ue": 10101, "bs": 10102}
QUEUE_LIMIT = 512  # max buffered messages per WS client


class Client:
    """One connected LMT client with a bounded outbound queue."""

    def __init__(self, ws):
        self.ws = ws
        self.queue: asyncio.Queue = asyncio.Queue(maxsize=QUEUE_LIMIT)
        self.dropped = 0

    def enqueue(self, message: str):
        try:
            self.queue.put_nowait(message)
        except asyncio.QueueFull:
            # Backpressure: shed the oldest entry, keep the newest flowing.
            try:
                self.queue.get_nowait()
                self.queue.put_nowait(message)
            except asyncio.QueueEmpty:
                pass
            self.dropped += 1
            if self.dropped % 100 == 1:
                print(f"[LogServer] client {self.ws.remote_address} slow; "
                      f"dropped {self.dropped} messages so far")

    async def sender(self):
        while True:
            message = await self.queue.get()
            await self.ws.send(message)


CLIENTS: set = set()
SEQ = 0


def tag_seq(log_str: str) -> str:
    """Attach the server-side monotonic _seq used by the LMT PDU store."""
    global SEQ
    SEQ += 1
    try:
        obj = json.loads(log_str)
        obj["_seq"] = SEQ
        return json.dumps(obj, separators=(",", ":"))
    except Exception:
        return log_str


class LogUdpProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport
        print(f"[LogServer] UDP receiver active on 0.0.0.0:{UDP_PORT}")

    def datagram_received(self, data, addr):
        global SEQ
        try:
            log_str = data.decode("utf-8").strip()
            log_data = json.loads(log_str)

            module = log_data.get("module", "SYS")
            level = log_data.get("level", "INFO")
            event = log_data.get("event", "GENERIC")
            fields = log_data.get("fields", {})
            SEQ += 1
            log_data["_seq"] = SEQ

            color = "\033[92m" if module == "UE" else ("\033[94m" if module == "BS" else "\033[90m")
            reset = "\033[0m"
            print(f"{color}[{module}][{level}]{reset} {event} - {json.dumps(fields)}")

            if CLIENTS:
                tagged = json.dumps(log_data, separators=(",", ":"))
                for client in list(CLIENTS):
                    client.enqueue(tagged)
        except Exception as e:
            print(f"[LogServer] Error: {e}", file=sys.stderr)


async def forward_command(target: str, cmd: str):
    loop = asyncio.get_running_loop()
    port = CMD_PORTS.get(target)
    if port is None or not cmd:
        return
    transport, _ = await loop.create_datagram_endpoint(
        lambda: asyncio.DatagramProtocol(),
        remote_addr=("127.0.0.1", port),
    )
    try:
        transport.sendto(cmd.encode("utf-8"))
        print(f"[LogServer] CMD -> {target}:{port}: {cmd!r}")
    finally:
        transport.close()


async def ws_handler(websocket):
    client = Client(websocket)
    print(f"[LogServer] LMT client connected from {websocket.remote_address}")
    CLIENTS.add(client)
    sender_task = asyncio.create_task(client.sender())
    try:
        async for raw in websocket:
            # Inbound frames are commands for the nodes.
            try:
                req = json.loads(raw)
                target = str(req.get("target", "ue")).lower()
                cmd = str(req.get("cmd", ""))
                if cmd:
                    await forward_command(target, cmd)
            except json.JSONDecodeError:
                print(f"[LogServer] ignoring non-JSON frame", file=sys.stderr)
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        sender_task.cancel()
        CLIENTS.discard(client)
        status = f", {client.dropped} dropped" if client.dropped else ""
        print(f"[LogServer] LMT client disconnected{status}")


async def main():
    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(
        lambda: LogUdpProtocol(),
        local_addr=("0.0.0.0", UDP_PORT),
    )

    async with websockets.serve(ws_handler, "0.0.0.0", WS_PORT):
        print(f"[LogServer] WebSocket server on ws://localhost:{WS_PORT}")
        print(f"[LogServer] command channel -> UE:{CMD_PORTS['ue']} BS:{CMD_PORTS['bs']}")
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[LogServer] Server terminated.")
