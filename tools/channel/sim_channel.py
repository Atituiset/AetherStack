#!/usr/bin/env python3
"""
AetherStack RF Channel Simulator (M6.5 T8)

UDP relay between UE and BS with configurable latency and packet loss.
Port topology (D3): traffic enters the channel on dedicated ingress ports
and is forwarded to the nodes' real listening ports, so UE/BS never bind
the same address twice and all flows traverse the channel.

    uplink   : UE -> [11001] --(loss/latency)--> BS :20002
    downlink : BS -> [21002] --(loss/latency)--> UE :10001

Physical-layer impairments (AWGN, fading) remain placeholders for Phase 1.
"""

import argparse
import random
import time
import socket


def main():
    parser = argparse.ArgumentParser(description="AetherStack Channel Simulator")
    parser.add_argument("--uplink-port", type=int, default=11001,
                        help="Ingress port for UE uplink bursts")
    parser.add_argument("--downlink-port", type=int, default=21002,
                        help="Ingress port for BS downlink bursts")
    parser.add_argument("--bs-dest-port", type=int, default=20002,
                        help="BS PHY listening port")
    parser.add_argument("--ue-dest-port", type=int, default=10001,
                        help="UE PHY listening port")
    parser.add_argument("--loss-rate", type=float, default=0.0,
                        help="Packet drop probability [0,1]")
    parser.add_argument("--latency", type=float, default=0.0,
                        help="Artificial latency in seconds")
    # Placeholder: SNR is not yet used for signal-level simulation
    parser.add_argument("--snr", type=float, default=99.0,
                        help="SNR in dB (placeholder for Phase 1)")
    parser.add_argument("--blackout", type=str, default="",
                        help="Drop-everything windows 'start:dur,start:dur' "
                             "in seconds from launch, e.g. '30:5,120:10'")
    parser.add_argument("--loss-schedule", type=str, default="",
                        help="Time-varying loss 'start:end:rate,...' seconds "
                             "from launch, e.g. '30:60:0.5' (overrides --loss-rate)")
    args = parser.parse_args()

    blackout_windows = []
    for spec in filter(None, args.blackout.split(",")):
        start_s, dur_s = spec.split(":")
        blackout_windows.append((float(start_s), float(dur_s)))

    loss_windows = []
    for spec in filter(None, args.loss_schedule.split(",")):
        s, e, r = spec.split(":")
        loss_windows.append((float(s), float(e), float(r)))

    def in_blackout(now: float) -> bool:
        return any(start <= now < start + dur for start, dur in blackout_windows)

    def loss_at(now: float) -> float:
        for s, e, r in loss_windows:
            if s <= now < e:
                return r
        return args.loss_rate

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))

    ul_ingress = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ul_ingress.bind(("127.0.0.1", args.uplink_port))
    ul_ingress.setblocking(False)

    dl_ingress = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dl_ingress.bind(("127.0.0.1", args.downlink_port))
    dl_ingress.setblocking(False)

    print("=" * 60)
    print("      AetherStack RF Channel Simulator (M6.5)")
    print("=" * 60)
    print(f"[*] Uplink  : UE -> 127.0.0.1:{args.uplink_port} -> BS:{args.bs_dest_port}")
    print(f"[*] Downlink: BS -> 127.0.0.1:{args.downlink_port} -> UE:{args.ue_dest_port}")
    print(f"[*] Loss: {args.loss_rate*100:.1f}% | Latency: {args.latency}s | SNR: {args.snr}dB (placeholder)")
    print("=" * 60)

    t0 = time.monotonic()

    def relay(ingress, dest_port, label):
        try:
            data, _ = ingress.recvfrom(65535)
        except BlockingIOError:
            return
        if in_blackout(time.monotonic() - t0):
            print(f"[Channel] BLACKOUT DROP {label}: {len(data)} bytes", flush=True)
            return
        if random.random() >= loss_at(time.monotonic() - t0):
            if args.latency > 0:
                time.sleep(args.latency)
            sock.sendto(data, ("127.0.0.1", dest_port))
            print(f"[Channel] RELAY {label}: {len(data)} bytes", flush=True)
        else:
            print(f"[Channel] DROP {label} : {len(data)} bytes", flush=True)

    if blackout_windows or loss_windows:
        wins = ", ".join(f"{s}s+{d}s" for s, d in blackout_windows)
        print(f"[*] Blackout windows: {wins}", flush=True)
    if loss_windows:
        sched = ", ".join(f"{s}-{e}s@{r:.0%}" for s, e, r in loss_windows)
        print(f"[*] Loss schedule: {sched}", flush=True)
    try:
        while True:
            relay(ul_ingress, args.bs_dest_port, "UPLINK  ")
            relay(dl_ingress, args.ue_dest_port, "DOWNLINK")
            time.sleep(0.0005)  # avoid a busy spin between bursts
    except KeyboardInterrupt:
        print("\n[*] Channel Simulator terminated.")
    finally:
        sock.close()
        ul_ingress.close()
        dl_ingress.close()


if __name__ == "__main__":
    main()
