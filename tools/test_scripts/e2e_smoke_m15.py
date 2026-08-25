#!/usr/bin/env python3
"""
M15 smoke: split-core cross-process end-to-end test.

Spawns amfd + upfd + bs + ue (four real processes). The gNB runs with
external core endpoints, so NAS attach and user-plane ping-pong must flow
through the AMF and UPF over the NG-like / GTP-U-like UDP links.

Exits 0 when every expected event is observed before the deadline.
"""

import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "bin"

EXPECTED = [
    ("AMF", "NG_SETUP_RX"),            # gNB registered with the AMF
    ("AMF", "NAS_ATTACH_ACCEPT_TX"),   # attach completed inside the AMF
    ("UE", "NAS_ATTACH_ACCEPT_RX"),
    ("UPF", "UPF_PATH_SWITCH"),        # UPF learned the UE route (UL_DATA)
    ("UE", "APP_RTT"),                 # user-plane round trip via the anchor
]
# Note: SEC_ENABLED (session-key delivery) requires an authenticated attach
# (USIM provisioned at the AMF); covered by the gtest suite instead.


class EventCollector:
    def __init__(self):
        self.seen = []
        self._lock = threading.Lock()
        self._threads = []

    def watch(self, proc):
        t = threading.Thread(target=self._pump, args=(proc,))
        t.daemon = True
        self._threads.append(t)
        t.start()

    def _pump(self, proc):
        for raw in proc.stdout:
            line = raw.decode("utf-8", errors="replace").strip()
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            with self._lock:
                self.seen.append((obj.get("module"), obj.get("event")))

    def raw(self):
        with self._lock:
            return list(self.seen)

    def missing(self):
        with self._lock:
            return [item for item in EXPECTED if item not in self.seen]


def main():
    procs = []
    collector = EventCollector()

    def spawn(name, cmd):
        print(f"[smoke15] spawn {name}: {' '.join(cmd)}")
        logf = os.environ.get("SMOKE_FILE_LOG")
        out = open(f"{logf}_{name}.log", "w") if logf else subprocess.PIPE
        p = subprocess.Popen(cmd, stdout=out,
                             stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        procs.append(p)
        if not logf:
            collector.watch(p)
        return p

    try:
        spawn("amfd", [str(BIN / "amfd"), "--log-port", "9998",
                       "--ng-port", "10110"])
        spawn("upfd", [str(BIN / "upfd"), "--log-port", "9998",
                       "--gu-port", "10120"])
        time.sleep(0.5)
        spawn("bs", [str(BIN / "bs"), "--log-port", "9998",
                     "--amf-addr", "127.0.0.1", "--amf-port", "10110",
                     "--ng-local-port", "20110",
                     "--upf-addr", "127.0.0.1", "--upf-port", "10120",
                     "--gu-local-port", "20120"])
        time.sleep(1.0)
        spawn("ue", [str(BIN / "ue"), "--log-port", "9998"])

        cmd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        deadline = time.time() + 40.0
        nudged = 0
        while time.time() < deadline:
            if collector.missing() and nudged < 5 and \
                    ("UE", "UE_ATTACH_START") not in collector.raw():
                cmd.sendto(b"attach", ("127.0.0.1", 10101))
                nudged += 1
            if not collector.missing():
                print("[smoke15] all expected events observed")
                break
            # nudge traffic periodically for the user-plane leg
            if nudged >= 1 and nudged < 8:
                cmd.sendto(b"send hello-m15", ("127.0.0.1", 10101))
                nudged += 1
            time.sleep(1.0)

        missing = collector.missing()
        if missing:
            print("[smoke15] MISSING events:", missing)
            return 1
        return 0
    finally:
        for p in procs:
            try:
                p.send_signal(signal.SIGTERM)
            except Exception:
                pass
        time.sleep(0.5)
        for p in procs:
            if p.poll() is None:
                p.kill()


if __name__ == "__main__":
    sys.exit(main())
