#!/usr/bin/env python3
"""
AetherStack RF Channel Simulator (M6.5 T8, reworked M19)

UDP relay between UE and BS with configurable latency and packet loss.
Port topology (D3): traffic enters the channel on dedicated ingress ports
and is forwarded to the nodes' real listening ports, so UE/BS never bind
the same address twice and all flows traverse the channel.

    uplink   : UE(s) -> [11001] --(loss/latency)--> BS :20002
    downlink : BS -> [21002] --(loss/latency)--> UE(s) :10001[,10002,...]

M19 changes:

* Parallel relaying: one reader thread per ingress direction and one
  worker thread per destination endpoint, connected by bounded queues
  (shed-oldest on overflow, like a real medium buffer). A bulky video
  burst destined for UE2 no longer serialises ahead of voice for UE3 —
  the M17 shared-FIFO head-of-line blocking is gone.
* Per-UE link quality: --ue-quality 10001=good,10002=mid,10003=poor maps
  each UE link (uplink by source port, downlink by destination port) to
  its own loss rate and an AWGN level given as SNR dB at unit transmit
  amplitude (absolute noise, so UL power control actually moves the
  received SNR). No flag = uniform legacy behaviour (global loss rate, no
  noise unless --awgn-snr is given).
"""

import argparse
import queue
import random
import socket
import struct
import threading
import time

# Per-link quality profiles: (loss_rate, snr_db_at_unit_amplitude).
# SNR values are calibrated against the receiver's MEASURED decode curve
# (docs/m19_plan.md): good holds 16QAM (~28 dB), mid is clean QPSK, poor is
# degraded-but-functional QPSK (attach survives on retries).
QUALITY_PROFILES = {
    "good": {"loss": 0.005, "snr_db": 28.0},
    "mid":  {"loss": 0.03,  "snr_db": 21.0},
    "poor": {"loss": 0.08,  "snr_db": 19.0},
    # M22: "bad" makes a cell effectively vanish for one UE (SIBs stop
    # decoding) — the mobility driver flips good<->bad to force handover.
    "bad":  {"loss": 0.15,  "snr_db": 15.0},
}

# Mean sample power of a transmitter burst at amplitude 1.0 (normalised
# IFFT over 64 subcarriers: ~1/64). Absolute-noise calibration reference —
# measured bursts sit at -18 dBmean.
REF_BURST_POWER = 1.0 / 64.0


def parse_ue_quality(spec):
    """'10001=good,10002=mid' -> {10001: {'loss':..,'snr_db':..}}."""
    out = {}
    for part in filter(None, (p.strip() for p in spec.split(","))):
        port_s, _, name = part.partition("=")
        if name not in QUALITY_PROFILES:
            raise ValueError(
                f"unknown quality '{name}' (choose from "
                f"{','.join(QUALITY_PROFILES)})")
        out[int(port_s)] = dict(QUALITY_PROFILES[name])
    return out


def parse_cell_quality(spec):
    """M22: '10001=1:good,10001=2:poor,...' -> {ue_port: {cell: profile}}."""
    out = {}
    for part in filter(None, (p.strip() for p in spec.split(","))):
        port_s, _, rest = part.partition("=")
        cell_s, _, name = rest.partition(":")
        if name not in QUALITY_PROFILES:
            raise ValueError(f"unknown quality '{name}'")
        out.setdefault(int(port_s), {})[int(cell_s)] =             dict(QUALITY_PROFILES[name])
    return out


def sigma_for_snr(snr_db):
    """Per-component AWGN sigma so a unit-amplitude burst sees snr_db."""
    import math
    return math.sqrt(REF_BURST_POWER / (2.0 * 10 ** (snr_db / 10.0)))


class EndpointWorker(threading.Thread):
    """One destination endpoint: latency + impair + send, serialised only
    per destination (no cross-endpoint head-of-line blocking)."""

    def __init__(self, name, dest_port, send_sock, latency_s, impair):
        super().__init__(daemon=True, name=name)
        self.dest_port = dest_port
        self.send_sock = send_sock
        self.latency_s = latency_s
        self.impair = impair  # fn(bytes) -> bytes
        self.q = queue.Queue(maxsize=2000)
        self.running = True

    def offer(self, data):
        if self.q.full():
            try:
                self.q.get_nowait()  # shed the oldest burst
            except queue.Empty:
                pass
        try:
            self.q.put_nowait(data)
        except queue.Full:
            pass

    def run(self):
        while self.running:
            try:
                data = self.q.get(timeout=0.2)
            except queue.Empty:
                continue
            if self.latency_s > 0:
                time.sleep(self.latency_s)
            try:
                self.send_sock.sendto(self.impair(data),
                                      ("127.0.0.1", self.dest_port))
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(description="AetherStack Channel Simulator")
    parser.add_argument("--uplink-port", type=int, default=11001,
                        help="Ingress port for UE uplink bursts")
    parser.add_argument("--downlink-port", type=str, default="21002",
                        help="Ingress port(s) for BS downlink bursts; "
                             "comma-separated, one per cell (M22)")
    parser.add_argument("--bs-dest-port", type=str, default="20002",
                        help="BS PHY listening port(s); comma-separated, "
                             "one per cell (M22)")
    parser.add_argument("--ue-dest-port", type=str, default="10001",
                        help="UE PHY listening port(s); comma-separated list "
                             "fans downlink out to every UE (broadcast medium)")
    parser.add_argument("--loss-rate", type=float, default=0.0,
                        help="Packet drop probability [0,1]")
    parser.add_argument("--latency", type=float, default=0.0,
                        help="Artificial latency in seconds")
    parser.add_argument("--snr", type=float, default=99.0,
                        help="SNR in dB (placeholder for Phase 1)")
    parser.add_argument("--blackout", type=str, default="",
                        help="Drop-everything windows 'start:dur,start:dur' "
                             "in seconds from launch, e.g. '30:5,120:10'")
    parser.add_argument("--loss-schedule", type=str, default="",
                        help="Time-varying loss 'start:end:rate,...' seconds "
                             "from launch, e.g. '30:60:0.5' (overrides --loss-rate)")
    parser.add_argument("--awgn-snr", type=float, default=0.0,
                        help="Add AWGN noise at this SNR in dB relative to each "
                             "burst's own power (0 = off, M10)")
    parser.add_argument("--multipath", type=str, default="",
                        help="One extra echo 'delay_samples,gain' (M10)")
    parser.add_argument("--ue-quality", type=str, default="",
                        help="Per-UE link quality "
                             "'port=good|mid|poor,...' (M19): per-link loss "
                             "rate + absolute AWGN (UL keyed by source port, "
                             "DL by destination port)")
    parser.add_argument("--cell-quality", type=str, default="",
                        help="M22 dual-cell: per-(UE,cell) quality "
                             "'10001=1:good,10001=2:bad,...'")
    parser.add_argument("--move-port", type=int, default=0,
                        help="M22: UDP control port for mobility commands "
                             "('move <ue_port> <cell>')")
    args = parser.parse_args()

    import math
    echo_delay = 0
    echo_gain = 0.0
    if args.multipath:
        delay_s, gain_s = args.multipath.split(",")
        echo_delay, echo_gain = int(delay_s), float(gain_s)

    try:
        import numpy as np
    except ImportError:
        np = None
        if args.awgn_snr > 0 or args.ue_quality:
            print("[!] numpy required for AWGN/per-link quality",
                  flush=True)
            raise SystemExit(2)

    ue_link_params = parse_ue_quality(args.ue_quality) \
        if args.ue_quality else {}
    # M22: per-(UE,cell) quality matrix. --ue-quality maps onto cell 1 for
    # backward compatibility; --cell-quality overrides per pair.
    matrix_lock = threading.Lock()
    cell_q = {p: {1: dict(l)} for p, l in ue_link_params.items()}
    for ue, cells in (parse_cell_quality(args.cell_quality)
                      if args.cell_quality else {}).items():
        cell_q.setdefault(ue, {}).update(cells)

    def link_for(ue_port, cell):
        """(loss_rate, impair_fn) for one (UE,cell) pair; falls back to
        the legacy single-cell behaviour when the matrix has no entry."""
        with matrix_lock:
            prof = cell_q.get(ue_port, {}).get(cell)
        if prof is not None:
            return prof["loss"], impair_absolute(sigma_for_snr(prof["snr_db"]))
        return None

    def impair_relative(data):
        """Legacy M10 impair: AWGN relative to each burst's own power
        (+ optional echo). Used only with --awgn-snr/--multipath."""
        if args.awgn_snr <= 0 and echo_gain == 0.0:
            return data
        if len(data) < 4:
            return data
        count = struct.unpack_from("<I", data, 0)[0]
        floats = np.frombuffer(data, dtype=np.float32,
                               count=2 * count, offset=4).copy()
        pwr = float((floats ** 2).mean())
        snr_lin = 10 ** (args.awgn_snr / 10)
        sigma = math.sqrt(pwr / snr_lin / 2) if snr_lin > 0 else 0.0
        if echo_gain != 0.0 and echo_delay < count:
            floats[2 * echo_delay::2] += echo_gain * floats[:2 * (count - echo_delay):2]
            floats[2 * echo_delay + 1::2] += echo_gain * floats[1:2 * (count - echo_delay) + 1:2]
        if sigma > 0:
            floats += np.random.normal(0, sigma, floats.shape).astype(np.float32)
        return struct.pack("<I", count) + floats.tobytes()

    def impair_absolute(sigma):
        """M19 per-link impair: FIXED absolute noise sigma, so a change in
        transmit amplitude (UL power control) moves the received SNR."""
        def fn(data):
            if sigma <= 0 or len(data) < 4:
                return data
            count = struct.unpack_from("<I", data, 0)[0]
            floats = np.frombuffer(data, dtype=np.float32,
                                   count=2 * count, offset=4).copy()
            floats += np.random.normal(0, sigma, floats.shape).astype(np.float32)
            return struct.pack("<I", count) + floats.tobytes()
        return fn

    blackout_windows = []
    for spec in filter(None, args.blackout.split(",")):
        start_s, dur_s = spec.split(":")
        blackout_windows.append((float(start_s), float(dur_s)))

    loss_windows = []
    for spec in filter(None, args.loss_schedule.split(",")):
        s, e, r = spec.split(":")
        loss_windows.append((float(s), float(e), float(r)))

    t0 = time.monotonic()

    def in_blackout():
        now = time.monotonic() - t0
        return any(start <= now < start + dur for start, dur in blackout_windows)

    def loss_for(port):
        """Loss probability for a packet on the link of `port`: per-link
        quality wins over the global rate/schedule."""
        link = ue_link_params.get(port)
        if link is not None:
            return link["loss"]
        now = time.monotonic() - t0
        for s, e, r in loss_windows:
            if s <= now < e:
                return r
        return args.loss_rate

    def impair_for(port):
        """DL/UL impair for a link: per-link absolute AWGN when the port has
        a quality profile, else the legacy relative/global one."""
        link = ue_link_params.get(port)
        if link is not None:
            return impair_absolute(sigma_for_snr(link["snr_db"]))
        return impair_relative

    ue_dest_ports = [int(p) for p in args.ue_dest_port.split(",") if p.strip()]
    bs_dest_ports = [int(p) for p in args.bs_dest_port.split(",") if p.strip()]
    dl_ingress_ports = [int(p) for p in args.downlink_port.split(",")
                        if p.strip()]
    # cell index per port: bs_dest_ports[i] and dl_ingress_ports[i] are
    # cell i+1 (lists must align).
    cell_of_bs = {port: i + 1 for i, port in enumerate(bs_dest_ports)}

    send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_sock.bind(("127.0.0.1", 0))

    ul_ingress = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ul_ingress.bind(("127.0.0.1", args.uplink_port))
    ul_ingress.setblocking(False)

    dl_ingresses = []
    for dp in dl_ingress_ports:
        s_ = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s_.bind(("127.0.0.1", dp))
        s_.setblocking(False)
        dl_ingresses.append((dp, s_))

    print("=" * 60)
    print("      AetherStack RF Channel Simulator (M19/M22)")
    print("=" * 60)
    print(f"[*] Uplink  : UE -> 127.0.0.1:{args.uplink_port} -> "
          f"BS:{','.join(str(p) for p in bs_dest_ports)}")
    print(f"[*] Downlink: BS -> 127.0.0.1:{','.join(str(p) for p in dl_ingress_ports)} -> "
          f"UE:{','.join(str(p) for p in ue_dest_ports)}")
    print(f"[*] Loss: {args.loss_rate*100:.1f}% | Latency: {args.latency}s")
    if ue_link_params:
        desc = ", ".join(f"{p}(loss={l['loss']:.1%},snr={l['snr_db']:.0f}dB)"
                         for p, l in sorted(ue_link_params.items()))
        print(f"[*] Per-link quality: {desc}", flush=True)
    if blackout_windows:
        wins = ", ".join(f"{s}s+{d}s" for s, d in blackout_windows)
        print(f"[*] Blackout windows: {wins}", flush=True)
    if loss_windows:
        sched = ", ".join(f"{s}-{e}s@{r:.0%}" for s, e, r in loss_windows)
        print(f"[*] Loss schedule: {sched}", flush=True)
    print("=" * 60, flush=True)

    # One worker per destination endpoint (each BS + each UE). Impairments
    # are applied by the readers at offer time (per source/dest link), so
    # every worker is identity-only.
    identity = lambda d: d
    workers = {}
    for port in bs_dest_ports + ue_dest_ports:
        w = EndpointWorker(f"dest-{port}", port, send_sock, args.latency,
                           identity)
        w.start()
        workers[port] = w

    counters = {"ul_ok": 0, "ul_drop": 0, "dl_ok": 0, "dl_drop": 0}
    counters_lock = threading.Lock()

    def bump(key):
        with counters_lock:
            counters[key] += 1

    def stats_printer():
        # Per-packet prints of the old loop flooded the demo log at media
        # rates; aggregate counters instead.
        while True:
            time.sleep(5)
            with counters_lock:
                snap = dict(counters)
            print(f"[Channel] 5s: ul={snap['ul_ok']}/{snap['ul_ok']+snap['ul_drop']} "
                  f"dl={snap['dl_ok']}/{snap['dl_ok']+snap['dl_drop']} relayed",
                  flush=True)

    threading.Thread(target=stats_printer, daemon=True,
                     name="stats").start()

    def ul_reader():
        while True:
            try:
                data, addr = ul_ingress.recvfrom(65535)
            except BlockingIOError:
                time.sleep(0.0005)
                continue
            black = in_blackout()
            for bs_port in bs_dest_ports:
                cell = cell_of_bs[bs_port]
                link = link_for(addr[1], cell)
                loss = link[0] if link else loss_for(addr[1])
                if black or random.random() < loss:
                    bump("ul_drop")
                    continue
                bump("ul_ok")
                impair = link[1] if link else impair_for(addr[1])
                workers[bs_port].offer(impair(data))

    def dl_reader(cell, ingress):
        while True:
            try:
                data, _ = ingress.recvfrom(65535)
            except BlockingIOError:
                time.sleep(0.0005)
                continue
            black = in_blackout()
            for port in ue_dest_ports:
                # Independent per-link fate: one UE's loss profile does not
                # leak into another UE's downlink copy.
                link = link_for(port, cell)
                loss = link[0] if link else loss_for(port)
                if black or random.random() < loss:
                    bump("dl_drop")
                    continue
                bump("dl_ok")
                impair = link[1] if link else impair_for(port)
                workers[port].offer(impair(data))

    def move_controller():
        """M22 mobility driver: 'move <ue_port> <cell>' flips that UE's
        link to good on the target cell and 'bad' (effectively dark) on
        the other — measurement reports then drive a handover."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", args.move_port))
        print(f"[*] Move control on UDP {args.move_port}", flush=True)
        while True:
            data, _ = sock.recvfrom(256)
            try:
                verb, ue_s, cell_s = data.decode().split()
                ue_port, target = int(ue_s), int(cell_s)
            except ValueError:
                continue
            if verb != "move" or target not in (1, 2):
                continue
            other = 1 if target == 2 else 2
            with matrix_lock:
                cell_q.setdefault(ue_port, {})[target] = \
                    dict(QUALITY_PROFILES["good"])
                cell_q[ue_port][other] = dict(QUALITY_PROFILES["bad"])
            print(f"[Channel] MOVE ue:{ue_port} -> cell {target} "
                  f"(cell {other} dark)", flush=True)

    threads = [
        threading.Thread(target=ul_reader, daemon=True, name="ul-reader"),
    ]
    for i, (dp, ingress) in enumerate(dl_ingresses):
        threads.append(threading.Thread(target=dl_reader, args=(i + 1, ingress),
                                        daemon=True, name=f"dl-reader-{dp}"))
    if args.move_port:
        threads.append(threading.Thread(target=move_controller, daemon=True,
                                        name="move-ctl"))
    for t in threads:
        t.start()

    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        print("\n[*] Channel Simulator terminated.")


if __name__ == "__main__":
    main()
