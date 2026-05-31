#!/usr/bin/env python3
"""
AetherStack Log Server

Receives structured JSON logs via UDP from C++ protocol stack processes,
and broadcasts them to all connected Web LMT clients via WebSocket.
"""

import asyncio
import json
import sys

import websockets

CONNECTED_CLIENTS: set = set()


class LogUdpProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport
        print("[LogServer] UDP receiver active on 0.0.0.0:9999")

    def datagram_received(self, data, addr):
        try:
            log_str = data.decode("utf-8").strip()
            log_data = json.loads(log_str)

            module = log_data.get("module", "SYS")
            level = log_data.get("level", "INFO")
            event = log_data.get("event", "GENERIC")
            fields = log_data.get("fields", {})

            # ANSI colored terminal output
            color = "\033[92m" if module == "UE" else ("\033[94m" if module == "BS" else "\033[90m")
            reset = "\033[0m"
            print(f"{color}[{module}][{level}]{reset} {event} - {json.dumps(fields)}")

            if CONNECTED_CLIENTS:
                asyncio.create_task(broadcast(log_str))
        except Exception as e:
            print(f"[LogServer] Error: {e}", file=sys.stderr)


async def broadcast(message: str):
    if not CONNECTED_CLIENTS:
        return
    clients = list(CONNECTED_CLIENTS)
    await asyncio.gather(
        *[client.send(message) for client in clients],
        return_exceptions=True,
    )


async def ws_handler(websocket, path="/"):
    print(f"[LogServer] LMT client connected from {websocket.remote_address}")
    CONNECTED_CLIENTS.add(websocket)
    try:
        async for _ in websocket:
            pass
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        CONNECTED_CLIENTS.discard(websocket)
        print("[LogServer] LMT client disconnected")


async def main():
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: LogUdpProtocol(),
        local_addr=("0.0.0.0", 9999),
    )

    async with websockets.serve(ws_handler, "0.0.0.0", 8765):
        print("[LogServer] WebSocket server on ws://localhost:8765")
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[LogServer] Server terminated.")
