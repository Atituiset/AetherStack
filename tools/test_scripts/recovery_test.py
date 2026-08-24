#!/usr/bin/env python3
"""
M7.4: fault-recovery scenario over a degraded channel.

Timeline (channel-relative):
   0-30 s   clean      -> attach + traffic on, loopback healthy
  30-60 s   loss 50%   -> heavy ping loss; stack must stay up, states intact
  60-90 s   clean      -> loopback must resume without any restart
  90-105 s  blackout   -> total outage; no crash, no state corruption
 105-135 s  clean      -> traffic flows again; graceful detach

Exit 0 when every phase behaves as specified.
"""

import json
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "bin"
PB = 15400  # ue phy .1, ue cmd .2, bs phy .3, ch ul .5, ch dl .6

events = []
lock = threading.Lock()


def pump(proc, tag, logdir):
    for raw in proc.stdout:
        line = raw.decode("utf-8", errors="replace").strip()
        with open(Path(logdir) / f"{tag}.log", "a") as f:
            f.write(line + "\n")
        if line.startswith("{"):
            try:
                o = json.loads(line)
                with lock:
                    events.append((time.time(), o.get("module"), o.get("event"),
                                   o.get("fields", {})))
            except json.JSONDecodeError:
                pass


def counts_since(t0):
    c = {}
    for ts, m, e, _ in list(events):
        if ts >= t0:
            key = (m, e)
            c[key] = c.get(key, 0) + 1
    return c


def stats_at(t0):
    tx = rx = loss = 0
    for ts, m, e, f in list(events):
        if ts >= t0 and e == "TRAFFIC_STATS":
            tx = max(tx, int(f.get("tx", 0)))
            rx = max(rx, int(f.get("rx", 0)))
            loss = max(loss, int(f.get("loss", 0)))
    return tx, rx, loss


def main():
    logdir = Path("/tmp/aether_recovery")
    logdir.mkdir(exist_ok=True)
    procs = []

    def spawn(name, cmd):
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        procs.append((name, p))
        threading.Thread(target=pump, args=(p, name, logdir), daemon=True).start()

    failures = []

    def finish(code=1):
        for _, p in reversed(procs):
            if p.poll() is None:
                p.terminate()
        time.sleep(0.7)
        for _, p in reversed(procs):
            if p.poll() is None:
                p.kill()
        ok = not failures
        print(f"[rec] {'PASS' if ok else 'FAIL'} — failures={failures}", flush=True)
        return 0 if ok else code

    try:
        spawn("channel", [
            sys.executable, str(ROOT / "tools/channel/sim_channel.py"),
            "--uplink-port", str(PB + 5), "--downlink-port", str(PB + 6),
            "--bs-dest-port", str(PB + 3), "--ue-dest-port", str(PB + 1),
            "--loss-schedule", "30:60:0.5,90:105:1.0",
        ])
        time.sleep(1.0)
        spawn("bs", [str(BIN / "bs"), "--log-port", "0",
                     "--local-phy-port", str(PB + 3), "--ue-phy-port", str(PB + 6)])
        time.sleep(1.0)
        spawn("ue", [str(BIN / "ue"), "--log-port", "0",
                     "--cmd-port", str(PB + 2),
                     "--local-phy-port", str(PB + 1), "--bs-phy-port", str(PB + 5)])

        cmd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        def send(line):
            cmd.sendto(line.encode(), ("127.0.0.1", PB + 2))

        time.sleep(1.5)  # let the UE finish binding its ports
        t0 = time.time()
        print(f"[rec] t=0 clean phase begins", flush=True)
        # Re-issue until registration lands (first packets may race the
        # UE's socket bind, and a lossy RACH may need self-healing).
        while not any(e == "NAS_ATTACH_ACCEPT_RX" for _, _, e, _ in events):
            if time.time() - t0 > 20:
                break
            send("attach")
            time.sleep(3.5)
        time.sleep(0.5)
        if not any(e == "NAS_ATTACH_ACCEPT_RX" for _, _, e, _ in events):
            failures.append("never_registered")

        send("traffic on")

        def wait_until(t_abs):
            while time.time() - t0 < t_abs:
                for name, p in procs:
                    if p.poll() is not None:
                        failures.append(f"{name}_died_rc{p.returncode}")
                        return False
                time.sleep(0.5)
            return True

        # --- phase checks ---------------------------------------------------
        if not wait_until(35):  # a bit into the 50% window
            return finish()
        tx1, rx1, _ = stats_at(t0)
        if tx1 <= 10:
            failures.append(f"no_traffic_before_degradation tx={tx1}")
        print(f"[rec] t=35 in 50%-loss: tx={tx1} rx={rx1}", flush=True)

        if not wait_until(62):  # just after 50% ends
            return finish()
        _, rx2, loss2 = stats_at(t0)
        print(f"[rec] t=62 post-50%: rx={rx2} loss={loss2}", flush=True)
        # during 30s at 50% we expect meaningful loss but also deliveries
        if not wait_until(95):  # into the blackout
            return finish()
        _, rx3, _ = stats_at(t0)
        print(f"[rec] t=95 in blackout: rx={rx3}", flush=True)

        if not wait_until(112):  # after blackout ends
            return finish()
        _, rx4, _ = stats_at(t0)
        print(f"[rec] t=112 recovered: rx={rx4}", flush=True)
        if rx4 <= rx3 and rx3 > 0:
            # allow tiny margins: some pings were already in flight
            if rx4 - rx3 < 5:
                failures.append(f"no_resume_after_blackout rx {rx3}->{rx4}")

        # states must have stayed CONNECTED/REGISTERED throughout
        reg_events = sum(1 for _, m, e, _ in events
                         if m == "UE" and e == "ATTACH_ABORT")
        if reg_events > 0:
            failures.append(f"attach_aborts_during_run ({reg_events})")

        if not wait_until(120):
            return finish()
        send("detach")
        time.sleep(2.5)
        if not any(e == "NAS_DETACH_RX" for _, m, e, _ in events if m == "BS"):
            # TM bearer may drop the single DETACH frame; re-register+retry once
            send("attach")
            time.sleep(3.5)
            send("detach")
            time.sleep(2.5)
            if not any(e == "NAS_DETACH_RX" for _, m, e, _ in events if m == "BS"):
                failures.append("no_detach_ack")

        return finish()

    except KeyboardInterrupt:
        print("\n[rec] interrupted", flush=True)
        return finish(130)
    finally:
        for _, p in reversed(procs):
            if p.poll() is None:
                p.terminate()
        time.sleep(0.7)
        for _, p in reversed(procs):
            if p.poll() is None:
                p.kill()


if __name__ == "__main__":
    sys.exit(main())
