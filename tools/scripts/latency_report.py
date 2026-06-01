#!/usr/bin/env python3
"""Latency report generator from AetherStack logs.

Parses log entries to compute round-trip times for user-plane data.

Usage:
    python latency_report.py < combined.log
"""

import json
import sys
import argparse
from collections import defaultdict


def main():
    parser = argparse.ArgumentParser(description="Generate latency report")
    parser.add_argument("--input", "-i", default=None, help="Input log file")
    args = parser.parse_args()

    if args.input:
        with open(args.input) as f:
            raw_lines = f.readlines()
    else:
        raw_lines = sys.stdin.readlines()

    send_times = {}
    rtts = []

    for line in raw_lines:
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue

        event = entry.get("event", "")
        fields = entry.get("fields", {})
        ts = entry.get("timestamp", "")

        if event == "APP_DATA_TX":
            seq = fields.get("seq", "0")
            send_times[seq] = ts
        elif event == "APP_DATA_RX":
            seq = fields.get("seq", "0")
            if seq in send_times:
                rtts.append({
                    "seq": seq,
                    "send_ts": send_times[seq],
                    "recv_ts": ts,
                })

    if not rtts:
        print("No RTT samples found.")
        return

    print(f"RTT Samples: {len(rtts)}")
    for r in rtts:
        print(f"  Seq {r['seq']}: sent={r['send_ts']} recv={r['recv_ts']}")


if __name__ == "__main__":
    main()
