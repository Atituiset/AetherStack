#!/usr/bin/env python3
"""
M6.5 T11: cross-process end-to-end smoke test.

Spawns the real `bs` and `ue` processes (optionally routed through
sim_channel), drives the UE via its UDP command channel and verifies the
full lifecycle in the emitted event stream:

    SIB sync -> RACH -> RRC setup -> NAS attach -> app ping-pong -> detach

Exits 0 when every expected event is observed before the deadline.
"""

import argparse
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

# (module, event) pairs that must appear in the combined stream.
EXPECTED = [
    ("BS", "BS_SIB_BROADCAST_ON"),
    ("UE", "UE_ATTACH_START"),
    ("UE", "RACH_SUCCESS"),
    ("UE", "NAS_ATTACH_ACCEPT_RX"),
    ("BS", "RRC_UE_CONNECTED"),
    ("BS", "APP_ECHO_TX"),
    ("UE", "APP_RTT"),
    ("BS", "NAS_DETACH_RX"),
    ("UE", "UE_DETACH_DONE"),
]


class EventCollector:
    """Merges JSON log lines from several process pipes into one stream."""

    def __init__(self):
        self.seen = []  # list of (module, event)
        self._lock = threading.Lock()
        self._threads = []

    def watch(self, proc):
        t = threading.Thread(target=self._pump, args=(proc,))
        t.daemon = False
        self._threads.append(t)
        t.start()

    def drain(self, timeout=3.0):
        """Wait for reader threads to finish consuming flushed output."""
        for t in self._threads:
            t.join(timeout)

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

    def missing(self):
        with self._lock:
            return [item for item in EXPECTED if item not in self.seen]

    def count(self):
        with self._lock:
            return len(self.seen)

    def raw(self):
        with self._lock:
            return list(self.seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", action="store_true",
                        help="route air traffic through sim_channel")
    parser.add_argument("--loss-rate", type=float, default=0.1,
                        help="channel loss rate (only with --channel)")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="max seconds to observe the full lifecycle")
    args = parser.parse_args()

    procs = []
    collector = EventCollector()

    def spawn(name, cmd):
        print(f"[smoke] spawn {name}: {' '.join(cmd)}")
        # SMOKE_FILE_LOG=<path> redirects node output to files instead of
        # the in-process collector (debugging aid).
        logf = os.environ.get("SMOKE_FILE_LOG")
        out = open(f"{logf}_{name}.log", "w") if logf else subprocess.PIPE
        p = subprocess.Popen(cmd, stdout=out,
                             stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        procs.append(p)
        if not logf:
            collector.watch(p)
        return p

    try:
        ue_phy_args, bs_phy_args = [], []
        if args.channel:
            spawn("channel", [
                sys.executable, str(ROOT / "tools/channel/sim_channel.py"),
                "--loss-rate", str(args.loss_rate),
            ])
            time.sleep(1.0)
            # D3 topology: UL enters channel on 11001, DL on 21002.
            ue_phy_args = ["--bs-phy-port", "11001"]
            bs_phy_args = ["--ue-phy-port", "21002"]

        spawn("bs", [str(BIN / "bs"), "--log-port", "9998", *bs_phy_args])
        time.sleep(1.0)
        spawn("ue", [str(BIN / "ue"), "--log-port", "9998", *ue_phy_args])

        # Drive the UE through its UDP command channel (T9).
        cmd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        def send_cmd(line):
            print(f"[smoke] cmd> {line}")
            cmd.sendto(line.encode(), ("127.0.0.1", 10101))

        time.sleep(2.5)   # let SIB broadcasts flow
        # Lossy channel: keep nudging attach until NAS registration lands.
        # Re-issue slower than the UE's 3 s guard window so every command
        # starts a fresh attempt instead of being swallowed mid-procedure.
        for _ in range(6):
            if ("UE", "NAS_ATTACH_ACCEPT_RX") in collector.raw():
                break
            send_cmd("attach")
            time.sleep(3.5)
        # Lossy channel: retry the ping until the loopback RTT is observed.
        for _ in range(6):
            if ("UE", "APP_RTT") in collector.raw():
                break
            send_cmd("send smoke-hello")
            time.sleep(1.0)
        # TM radio bearer has no retransmission; if the first DETACH frame is
        # dropped the UE is already locally detached and ignores repeats, so
        # re-register and detach again (what a real terminal would do).
        for _ in range(4):
            if ("BS", "NAS_DETACH_RX") in collector.raw():
                break
            send_cmd("attach")
            time.sleep(3.5)
            send_cmd("detach")
            time.sleep(1.0)

        deadline = time.time() + args.timeout
        while time.time() < deadline:
            if not collector.missing():
                break
            time.sleep(0.2)

        print(f"\n[smoke] observed {collector.count()} protocol events")
        dump = os.environ.get("SMOKE_DUMP")
        if dump:
            with open(dump, "w") as f:
                for item in collector.raw():
                    f.write(json.dumps(item) + "\n")
            print(f"[smoke] dumped seen stream to {dump}")
        missing = collector.missing()
        if not missing:
            print("[smoke] PASS: full lifecycle observed "
                  "(SIB->RACH->RRC->NAS->data->detach)")
            return 0
        print(f"[smoke] FAIL: missing events: {missing}", file=sys.stderr)
        return 1
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
        time.sleep(0.5)
        for p in reversed(procs):
            if p.poll() is None:
                p.kill()
        collector.drain(3.0)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    sys.exit(main())
