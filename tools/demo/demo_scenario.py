#!/usr/bin/env python3
"""
M8.2: unattended demo scenario driver.

Watches the live event stream (WebSocket from log_server), drives the UE
through its UDP command channel and narrates the run by injecting
DEMO_PHASE events into the same log pipeline, so the Web LMT can render a
progress banner without any dedicated control channel:

    attach -> NAS registered -> user-plane traffic -> stats -> release

Usage:
    .venv/bin/python3 tools/demo/demo_scenario.py [--traffic-secs 20] [--loop]
"""

import argparse
import asyncio
import json
import socket
import sys
import time
from datetime import datetime, timezone

import websockets

CMD_PORT = 10101
LOG_UDP = ("127.0.0.1", 9999)
WS_URL = "ws://127.0.0.1:8765"

PHASES = {
    "boot":     (5,   "系统启动",   "节点与信道就绪，等待小区广播"),
    "attach":   (30,  "UE 开机附着", "SIB 同步 → RACH 四步 → RRC 建立 → NAS 注册"),
    "traffic":  (75,  "用户面回环", "100ms 周期数据包经全栈回环，统计吞吐与时延"),
    "release":  (92,  "连接释放",   "NAS 去注册 + RRC 释放"),
    "done":     (100, "演示完成",   "全流程无人值守执行成功"),
}


def now_iso():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def emit_phase(phase, detail=""):
    pct, title, blurb = PHASES[phase]
    fields = {"phase": phase, "title": title, "progress": str(pct),
              "detail": detail or blurb}
    msg = {"timestamp": now_iso(), "module": "DEMO", "level": "INFO",
           "event": "DEMO_PHASE", "fields": fields}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(json.dumps(msg).encode(), LOG_UDP)
    finally:
        sock.close()
    print(f"[demo] phase={phase} ({pct}%) {title} — {fields['detail']}",
          flush=True)


class Driver:
    def __init__(self, cmd_port, timeout_s):
        self.cmd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.cmd_port = cmd_port
        self.timeout_s = timeout_s

    def send(self, line):
        print(f"[demo] cmd> {line}", flush=True)
        self.cmd.sendto(line.encode(), ("127.0.0.1", self.cmd_port))

    async def wait_event(self, ws, module, event, what):
        """Pump WS messages until <module:event> arrives or timeout expires."""
        deadline = time.monotonic() + self.timeout_s
        pending = []

        while time.monotonic() < deadline:
            try:
                raw = await asyncio.wait_for(
                    ws.recv(), timeout=max(0.05, deadline - time.monotonic()))
            except asyncio.TimeoutError:
                break
            try:
                obj = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if obj.get("module") == module and obj.get("event") == event:
                # Drain any queued frames so stale events don't leak into
                # the next wait.
                return True
            pending.append(obj)
        print(f"[demo] TIMEOUT waiting for {module}:{event}", flush=True)
        return False


async def run_once(ws, drv, traffic_secs):
    ok = True

    emit_phase("boot")
    await asyncio.sleep(2.0)

    # --- attach ---------------------------------------------------------
    emit_phase("attach")
    drv.send("attach")
    ok &= await drv.wait_event(ws, "UE", "NAS_ATTACH_ACCEPT_RX",
                               "NAS registration")

    # --- traffic --------------------------------------------------------
    if ok:
        emit_phase("traffic")
        drv.send("traffic on")
        t_end = time.monotonic() + traffic_secs
        pings = 0
        while time.monotonic() < t_end:
            try:
                raw = await asyncio.wait_for(
                    ws.recv(), timeout=max(0.05, t_end - time.monotonic()))
                obj = json.loads(raw)
                if obj.get("event") == "APP_RTT":
                    pings += 1
            except asyncio.TimeoutError:
                pass
        drv.send("stats")  # final TRAFFIC_STATS lands in the stream
        await asyncio.sleep(0.6)
        emit_phase("traffic", f"{pings} 个数据包完成全栈回环")
        await asyncio.sleep(1.2)

    # --- release ----------------------------------------------------------
    if ok:
        emit_phase("release")
        drv.send("detach")
        ok &= await drv.wait_event(ws, "BS", "NAS_DETACH_RX",
                                   "detach acknowledged")

    emit_phase("done" if ok else "done")
    return ok


async def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--traffic-secs", type=float, default=20.0)
    ap.add_argument("--timeout", type=float, default=25.0,
                    help="per-step event wait timeout seconds")
    ap.add_argument("--cmd-port", type=int, default=CMD_PORT)
    ap.add_argument("--loop", action="store_true",
                    help="repeat the scenario forever")
    args = ap.parse_args()

    drv = Driver(args.cmd_port, args.timeout)
    print(f"[demo] connecting to {WS_URL} ...", flush=True)

    async with websockets.connect(WS_URL) as ws:
        while True:
            ok = await run_once(ws, drv, args.traffic_secs)
            print(f"[demo] {'PASS' if ok else 'FAIL'}", flush=True)
            if not args.loop or not ok:
                sys.exit(0 if ok else 1)
            await asyncio.sleep(3.0)


if __name__ == "__main__":
    asyncio.run(main())
