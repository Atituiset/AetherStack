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
CMD_PORT_UE2 = 10103
LOG_UDP = ("127.0.0.1", 9999)
WS_URL = "ws://127.0.0.1:8765"

PHASES = {
    "boot":     (5,   "系统启动",   "节点与信道就绪，等待小区广播"),
    "attach":   (30,  "UE 开机附着", "SIB 同步 → RACH 四步 → RRC 建立 → NAS 注册"),
    "u2u":      (55,  "UE 间通信",  "UE1 经 BS 转发向 UE2 发消息；SIP 振铃→自动应答→语音通话"),
    "traffic":  (75,  "用户面回环", "100ms 周期数据包经全栈回环，统计吞吐与时延"),
    "release":  (92,  "连接释放",   "NAS 去注册 + RRC 释放"),
    "done":     (100, "演示完成",   "全流程无人值守执行成功"),
}

# UE2's IMSI is its "phone number" for the U2U showcase (matches start_demo.sh).
UE2_IMSI = "460011234567891"


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
        # Events consumed from the stream but not matched by any wait yet.
        # Without this, an event that arrives out of order (e.g. ue2's
        # attach accept BEFORE ue1's) is swallowed by the wait for its
        # sibling and the later wait can never succeed.
        self.backlog = []

    def send(self, line):
        print(f"[demo] cmd> {line}", flush=True)
        self.cmd.sendto(line.encode(), ("127.0.0.1", self.cmd_port))

    async def wait_event(self, ws, module, event, what, node=None):
        """Pump WS messages until <module:event> arrives or timeout expires.

        If node is given, only a record with that top-level "node" field
        (e.g. "ue1") satisfies the wait -- needed in the two-UE demo where
        both UEs emit the same module:event pairs."""
        def hit(o):
            return o.get("module") == module and o.get("event") == event \
                and (node is None or o.get("node") == node)

        # An earlier wait may already have consumed our event.
        for i, o in enumerate(self.backlog):
            if hit(o):
                self.backlog.pop(i)
                return True

        deadline = time.monotonic() + self.timeout_s
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
            if hit(obj):
                return True
            # Keep non-matching events for later waits (PDU_TRACE dominates
            # the volume and no wait targets it, so drop only that).
            if obj.get("event") != "PDU_TRACE":
                self.backlog.append(obj)
        print(f"[demo] TIMEOUT waiting for {module}:{event}", flush=True)
        return False


async def run_once(ws, drv, drv2, traffic_secs):
    ok = True

    emit_phase("boot")
    await asyncio.sleep(2.0)

    # --- attach ---------------------------------------------------------
    # Two-UE demo: both UEs attach; user-plane traffic below runs on UE1.
    emit_phase("attach")
    drv.send("attach")
    drv2.send("attach")
    ok &= await drv.wait_event(ws, "UE", "NAS_ATTACH_ACCEPT_RX",
                               "NAS registration", node="ue1")
    ok &= await drv.wait_event(ws, "UE", "NAS_ATTACH_ACCEPT_RX",
                               "NAS registration (ue2)", node="ue2")

    # --- UE-to-UE: text message + SIP-lite voice call through the BS -----
    if ok:
        emit_phase("u2u")
        drv.send(f"msg {UE2_IMSI} hello-from-ue1")
        ok &= await drv.wait_event(ws, "UE", "APP_MSG_RX",
                                   "message delivered to ue2", node="ue2")
    if ok:
        # INVITE -> 180 -> auto-answer (~4 s default) -> 200/ACK -> media.
        drv.send(f"call {UE2_IMSI}")
        ok &= await drv.wait_event(ws, "UE", "SIP_RINGING_TX",
                                   "ue2 ringing", node="ue2")
    if ok:
        ok &= await drv.wait_event(ws, "UE", "SIP_CALL_ESTABLISHED",
                                   "dialog established (auto-answer)",
                                   node="ue2")
    if ok:
        await asyncio.sleep(3.0)  # voice media flows; APP_STREAM_STATS flows
        drv.send("call end")     # BYE
        ok &= await drv.wait_event(ws, "UE", "APP_CALL_PEER_END",
                                   "peer hangup on ue2", node="ue2")

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
        drv2.send("detach")
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
    ap.add_argument("--cmd-port2", type=int, default=CMD_PORT_UE2,
                    help="UE2 command port (attach/detach only)")
    ap.add_argument("--loop", action="store_true",
                    help="repeat the scenario forever")
    args = ap.parse_args()

    drv = Driver(args.cmd_port, args.timeout)
    drv2 = Driver(args.cmd_port2, args.timeout)
    print(f"[demo] connecting to {WS_URL} ...", flush=True)

    async with websockets.connect(WS_URL) as ws:
        while True:
            ok = await run_once(ws, drv, drv2, args.traffic_secs)
            print(f"[demo] {'PASS' if ok else 'FAIL'}", flush=True)
            if not args.loop or not ok:
                sys.exit(0 if ok else 1)
            await asyncio.sleep(3.0)


if __name__ == "__main__":
    asyncio.run(main())
