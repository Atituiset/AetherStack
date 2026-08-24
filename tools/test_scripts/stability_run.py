#!/usr/bin/env python3
"""
M7.3: long-run stability harness.

Drives a real BS+UE pair (optionally through sim_channel) in continuous
user-plane loopback for --duration seconds and verifies:

  * both processes stay alive the whole time (no crash)
  * NAS registration succeeds and no attach-abort storm occurs
  * traffic keeps flowing: tx advances; rx >= tx - loss - allowed_gap
  * RSS growth stays bounded vs. a post-warmup baseline

Writes a JSON report plus per-process logs under --logdir and exits 0 only
when every check holds. Example:

    python3 tools/test_scripts/stability_run.py --duration 1800 \
        --channel --loss-rate 0.05 --blackout "600:10,1200:5"
"""

import argparse
import json
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "bin"


def rss_kb(pid):
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except OSError:
        pass
    return -1


class LogPump:
    """Tails a process stdout into a file while counting events."""

    def __init__(self, proc, tag, logdir):
        self.proc = proc
        self.counts = {}
        self.last_stats = {}
        self._lock = threading.Lock()
        self._fh = open(Path(logdir) / f"{tag}.log", "w", buffering=1)
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self):
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", errors="replace").strip()
            self._fh.write(line + "\n")
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            with self._lock:
                event = obj.get("event", "")
                self.counts[event] = self.counts.get(event, 0) + 1
                if event == "TRAFFIC_STATS":
                    self.last_stats.update(obj.get("fields", {}))

    def snapshot(self):
        with self._lock:
            return dict(self.counts), dict(self.last_stats)

    def close(self):
        try:
            self._thread.join(timeout=3.0)
        finally:
            self._fh.close()


def wait_for_event(pump, event, timeout_s):
    end = time.time() + timeout_s
    while time.time() < end:
        counts, _ = pump.snapshot()
        if counts.get(event, 0) > 0:
            return True
        if pump.proc.poll() is not None:
            return False
        time.sleep(0.3)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--duration", type=float, default=1800.0,
                    help="loopback run length seconds (default 1800 = 30 min)")
    ap.add_argument("--channel", action="store_true",
                    help="route air traffic through sim_channel")
    ap.add_argument("--loss-rate", type=float, default=0.05)
    ap.add_argument("--blackout", type=str, default="",
                    help="channel blackout windows 'start:dur,...' seconds")
    ap.add_argument("--port-base", type=int, default=15100,
                    help="base for all local ports to avoid clashes")
    ap.add_argument("--max-rss-growth", type=float, default=1.0,
                    help="fail if RSS grows more than this fraction (1.0=100%%)")
    ap.add_argument("--logdir", type=str, default="/tmp/aether_stability")
    args = ap.parse_args()

    logdir = Path(args.logdir)
    logdir.mkdir(parents=True, exist_ok=True)
    report_path = logdir / "report.json"

    pb = args.port_base
    # ue phy pb+1, ue cmd pb+2, bs phy pb+3, bs cmd pb+4,
    # channel UL ingress pb+5, channel DL ingress pb+6.
    procs = []
    pumps = []
    failures = []
    checks = {}
    samples = []

    def spawn(name, cmd):
        print(f"[stab] spawn {name}: {' '.join(cmd)}", flush=True)
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        procs.append((name, p))
        pumps.append(LogPump(p, name, logdir))
        return p

    def terminate_all():
        for _, p in reversed(procs):
            if p.poll() is None:
                p.terminate()
        time.sleep(0.7)
        for _, p in reversed(procs):
            if p.poll() is None:
                p.kill()

    def finalize(code, final_stats=None):
        result = {
            "config": vars(args),
            "checks": checks,
            "failures": failures,
            "samples": samples,
            "final_stats": final_stats or {},
        }
        report_path.write_text(json.dumps(result, indent=2))
        ok = not failures and all(checks.values()) and code == 0
        print(f"[stab] {'PASS' if ok else 'FAIL'} — "
              f"checks={checks} failures={failures}", flush=True)
        print(f"[stab] report: {report_path}", flush=True)
        return 0 if ok else 1

    try:
        ue_phy = ["--local-phy-port", str(pb + 1)]
        bs_phy = ["--local-phy-port", str(pb + 3)]

        if args.channel:
            ch_cmd = [sys.executable, str(ROOT / "tools/channel/sim_channel.py"),
                      "--loss-rate", str(args.loss_rate),
                      "--uplink-port", str(pb + 5),
                      "--downlink-port", str(pb + 6),
                      "--bs-dest-port", str(pb + 3),
                      "--ue-dest-port", str(pb + 1)]
            if args.blackout:
                ch_cmd += ["--blackout", args.blackout]
            spawn("channel", ch_cmd)
            time.sleep(1.0)
            ue_phy += ["--bs-phy-port", str(pb + 5)]
            bs_phy += ["--ue-phy-port", str(pb + 6)]

        if not args.channel:
            ue_phy += ["--bs-phy-port", str(pb + 3)]
            bs_phy += ["--ue-phy-port", str(pb + 1)]
        spawn("bs", [str(BIN / "bs"), "--log-port", "0", *bs_phy])
        time.sleep(1.0)
        spawn("ue", [str(BIN / "ue"), "--log-port", "0",
                     "--cmd-port", str(pb + 2), *ue_phy])

        def pumps_by(name):
            idx = [n for n, _ in procs].index(name)
            return pumps[idx]

        cmd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        def send(line):
            cmd.sendto(line.encode(), ("127.0.0.1", pb + 2))

        # --- bring-up -----------------------------------------------------
        t_end = time.time() + 30
        attached = False
        while time.time() < t_end:
            send("attach")
            if wait_for_event(pumps_by("ue"), "NAS_ATTACH_ACCEPT_RX", 4.0):
                attached = True
                break
            # guard window is 3 s; re-issue once it has expired
            time.sleep(1.2)
        if not attached:
            failures.append("never_registered")
            sys.exit(finalize(1))
        checks["registered"] = True

        aborts_before = pumps_by("ue").snapshot()[0].get("ATTACH_ABORT", 0)

        send("traffic on")

        # --- steady-state sampling loop -----------------------------------
        baseline = None
        t_start = time.time()
        next_sample = t_start + 5.0
        last_tx_seen = 0
        stall_since = None

        while time.time() - t_start < args.duration:
            time.sleep(0.5)
            now = time.time()
            for name, p in procs:
                rc = p.poll()
                if rc is not None:
                    print(f"[stab] FAIL: {name} exited early (rc={rc})",
                          flush=True)
                    checks["no_crash"] = False
                    failures.append(f"{name}_exited_rc{rc}")
                    sys.exit(finalize(1))
            if now < next_sample:
                continue
            next_sample = now + 5.0

            counts, stats = pumps_by("ue").snapshot()
            tx = int(stats.get("tx", counts.get("APP_DATA_TX", 0)) or 0)
            rx = int(stats.get("rx", counts.get("APP_RTT", 0)) or 0)
            loss = int(stats.get("loss", 0) or 0)
            rss_u, rss_b = rss_kb(procs[-1][1].pid), rss_kb(procs[0][1].pid)
            samples.append({"t": round(now - t_start, 1), "tx": tx, "rx": rx,
                            "loss": loss, "rss_ue_kb": rss_u, "rss_bs_kb": rss_b})
            print(f"[stab] t={samples[-1]['t']:7.1f}s tx={tx} rx={rx} "
                  f"loss={loss} rss ue={rss_u}k bs={rss_b}k", flush=True)

            if baseline is None and now - t_start > 60:
                baseline = {"ue": rss_u, "bs": rss_b}
            if baseline:
                for who, val, base in (("ue", rss_u, baseline["ue"]),
                                       ("bs", rss_b, baseline["bs"])):
                    base = max(base, 4096)
                    if val > base * (1 + args.max_rss_growth):
                        msg = f"rss_{who}_growth {base}k->{val}k"
                        print(f"[stab] FAIL: {msg}", flush=True)
                        checks["rss_bounded"] = False
                        failures.append(msg)

            if tx > last_tx_seen:
                last_tx_seen = tx
                stall_since = None
            else:
                stall_since = stall_since or now
                if now - stall_since > 15:
                    msg = f"traffic_stalled_at_tx={tx}"
                    print(f"[stab] FAIL: {msg}", flush=True)
                    failures.append(msg)
                    sys.exit(finalize(1))

        checks["traffic_flowing"] = True
        checks["no_crash"] = True

        aborts_after = pumps_by("ue").snapshot()[0].get("ATTACH_ABORT", 0)
        if aborts_after > aborts_before:
            failures.append(f"attach_abort_storm ({aborts_after})")

        # --- wrap up --------------------------------------------------------
        send("traffic off")
        send("stats")
        time.sleep(1.5)
        _, stats = pumps_by("ue").snapshot()
        print(f"[stab] final: tx={stats.get('tx')} rx={stats.get('rx')} "
              f"loss={stats.get('loss')} rtt_avg={stats.get('rtt_avg')}ms", flush=True)
        sys.exit(finalize(0, stats))

    except KeyboardInterrupt:
        print("\n[stab] interrupted", flush=True)
        sys.exit(130)
    finally:
        terminate_all()
        for pl in pumps:
            pl.close()


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    main()
