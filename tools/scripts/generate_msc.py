#!/usr/bin/env python3
"""Generate Mermaid sequence diagram from AetherStack JSON logs.

Usage:
    python generate_msc.py < combined.log > msc.md
    python generate_msc.py --input combined.log --output msc.md
"""

import json
import sys
import argparse

EVENT_MESSAGES = {
    "MAC_RACH_MSG1": ("UE", "BS", "MSG1: PRACH Preamble"),
    "MAC_RACH_MSG2": ("BS", "UE", "MSG2: RAR"),
    "MAC_RACH_MSG3": ("UE", "BS", "MSG3: RRC Setup Request"),
    "MAC_RACH_MSG4": ("BS", "UE", "MSG4: Contention Resolve"),
    "RRC_SETUP_REQUEST_TX": ("UE", "BS", "RRC Setup Request"),
    "RRC_SETUP_TX": ("BS", "UE", "RRC Setup"),
    "RRC_SETUP_COMPLETE_TX": ("UE", "BS", "RRC Setup Complete"),
    "NAS_ATTACH_REQUEST": ("UE", "BS", "NAS Attach Request"),
    "NAS_ATTACH_ACCEPT_TX": ("BS", "UE", "NAS Attach Accept"),
}

NOTE_EVENTS = {
    "RACH_SUCCESS": ("UE", "RACH Success"),
    "RRC_UE_CONNECTED": ("BS", "RRC Connected"),
    "MAC_STATE_CHANGE": (None, None),
    "RRC_MIB_RX": (None, None),
    "RRC_SIB1_RX": (None, None),
}


def parse_logs(lines):
    entries = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
            entries.append(entry)
        except json.JSONDecodeError:
            continue
    return entries


def generate_mermaid(entries):
    lines = ["sequenceDiagram", "    participant UE", "    participant BS", ""]

    for entry in entries:
        event = entry.get("event", "")
        fields = entry.get("fields", {})

        if event in EVENT_MESSAGES:
            src, dst, label = EVENT_MESSAGES[event]
            extra = ""
            if "c_rnti" in fields:
                extra = f" (C-RNTI={fields['c_rnti']})"
            elif "ra_rnti" in fields:
                extra = f" (RA-RNTI={fields['ra_rnti']})"
            elif "tmsi" in fields:
                extra = f" (TMSI={fields['tmsi']})"
            elif "preamble" in fields:
                extra = f" (preamble={fields['preamble']})"
            lines.append(f"    {src}->>{dst}: {label}{extra}")

        elif event in NOTE_EVENTS:
            participant, text = NOTE_EVENTS[event]
            if participant and text:
                lines.append(f"    Note over {participant}: {text}")

        elif event == "RRC_UE_STATE":
            new_state = fields.get("new", "")
            if new_state == "CONNECTED":
                lines.append("    Note over UE: RRC Connected")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate MSC from AetherStack logs")
    parser.add_argument("--input", "-i", default=None, help="Input log file")
    parser.add_argument("--output", "-o", default=None, help="Output mermaid file")
    args = parser.parse_args()

    if args.input:
        with open(args.input) as f:
            raw_lines = f.readlines()
    else:
        raw_lines = sys.stdin.readlines()

    entries = parse_logs(raw_lines)
    mermaid = generate_mermaid(entries)

    if args.output:
        with open(args.output, "w") as f:
            f.write(mermaid + "\n")
    else:
        print(mermaid)


if __name__ == "__main__":
    main()
