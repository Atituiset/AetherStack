#!/usr/bin/env python3
"""
AetherStack RF Channel Simulator (MVP)

A simple UDP relay between UE and BS with configurable latency and packet loss.
Physical-layer impairments (AWGN, fading) are placeholders for Phase 1.
"""

import argparse
import random
import socket
import time


def main():
    parser = argparse.ArgumentParser(description="AetherStack Channel Simulator")
    parser.add_argument("--ue-port", type=int, default=10001, help="UE uplink source port")
    parser.add_argument("--bs-port", type=int, default=10002, help="BS downlink source port")
    parser.add_argument("--ue-dest-port", type=int, default=20001, help="Port where UE listens")
    parser.add_argument("--bs-dest-port", type=int, default=20002, help="Port where BS listens")
    parser.add_argument("--loss-rate", type=float, default=0.0, help="Packet drop probability [0,1]")
    parser.add_argument("--latency", type=float, default=0.0, help="Artificial latency in seconds")
    # Placeholder: SNR is not yet used for signal-level simulation
    parser.add_argument("--snr", type=float, default=99.0, help="SNR in dB (placeholder for Phase 1)")
    args = parser.parse_args()

    # Relay socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))

    # Listeners
    ue_listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ue_listener.bind(("127.0.0.1", args.ue_port))
    ue_listener.setblocking(False)

    bs_listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bs_listener.bind(("127.0.0.1", args.bs_port))
    bs_listener.setblocking(False)

    print("=" * 50)
    print("      AetherStack RF Channel Simulator (MVP)")
    print("=" * 50)
    print(f"[*] UE Uplink  : 127.0.0.1:{args.ue_port} -> 127.0.0.1:{args.bs_dest_port}")
    print(f"[*] BS Downlink: 127.0.0.1:{args.bs_port} -> 127.0.0.1:{args.ue_dest_port}")
    print(f"[*] Loss: {args.loss_rate*100:.1f}% | Latency: {args.latency}s | SNR: {args.snr}dB (placeholder)")
    print("=" * 50)

    try:
        while True:
            # UE -> BS (Uplink)
            try:
                data, _ = ue_listener.recvfrom(65535)
                if random.random() >= args.loss_rate:
                    if args.latency > 0:
                        time.sleep(args.latency)
                    sock.sendto(data, ("127.0.0.1", args.bs_dest_port))
                    print(f"[Channel] RELAY UPLINK  : {len(data)} bytes")
                else:
                    print(f"[Channel] DROP UPLINK   : {len(data)} bytes")
            except BlockingIOError:
                pass

            # BS -> UE (Downlink)
            try:
                data, _ = bs_listener.recvfrom(65535)
                if random.random() >= args.loss_rate:
                    if args.latency > 0:
                        time.sleep(args.latency)
                    sock.sendto(data, ("127.0.0.1", args.ue_dest_port))
                    print(f"[Channel] RELAY DOWNLINK: {len(data)} bytes")
                else:
                    print(f"[Channel] DROP DOWNLINK : {len(data)} bytes")
            except BlockingIOError:
                pass

            time.sleep(0.001)
    except KeyboardInterrupt:
        print("\n[*] Channel Simulator terminated.")
    finally:
        sock.close()
        ue_listener.close()
        bs_listener.close()


if __name__ == "__main__":
    main()
