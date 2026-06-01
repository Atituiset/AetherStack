#!/usr/bin/env python3
"""PDU hex analyzer for AetherStack protocol stack.

Parses PDU_TRACE log entries and decodes each layer:
MAC subheader → RLC TM → PDCP header → RRC/NAS TLV

Usage:
    python pdu_analyzer.py < pdu_trace.log
    python pdu_analyzer.py --input pdu_trace.log
"""

import json
import sys
import argparse


def parse_mac_subheader(data):
    """Parse simplified MAC subheader: [R|F|LCID] [+ L] [+ L_hi L_lo]"""
    if not data:
        return None, data
    byte0 = data[0]
    lcid = byte0 & 0x3F
    f_bit = (byte0 >> 6) & 1
    r_bit = (byte0 >> 7) & 1

    pos = 1
    if f_bit == 0:
        if pos >= len(data):
            return None, data
        length = data[pos]
        pos += 1
    else:
        if pos + 1 >= len(data):
            return None, data
        length = (data[pos] << 8) | data[pos + 1]
        pos += 2

    payload = data[pos:pos + length]
    rest = data[pos + length:]
    return {
        "layer": "MAC",
        "r": r_bit,
        "f": f_bit,
        "lcid": lcid,
        "length": length,
        "payload_hex": payload.hex(":") if payload else "",
        "payload_bytes": payload,
    }, rest


def parse_pdcp_header(data):
    """Parse PDCP header: [version|reserved] [seq_num_lo]"""
    if len(data) < 2:
        return None, data
    version = (data[0] >> 4) & 0xF
    seq_num = data[1]
    payload = data[2:]
    return {
        "layer": "PDCP",
        "version": version,
        "seq_num": seq_num,
        "payload_hex": payload.hex(":") if payload else "",
        "payload_bytes": payload,
    }, []


def parse_rrc_message(data):
    """Parse RRC TLV: [msg_type] [len_lo] [len_hi] [value...]"""
    if len(data) < 3:
        return None
    msg_type = data[0]
    length = data[1] | (data[2] << 8)
    value = data[3:3 + length]
    type_names = {1: "SetupRequest", 2: "Setup", 3: "SetupComplete", 4: "Release"}
    return {
        "layer": "RRC",
        "msg_type": msg_type,
        "msg_name": type_names.get(msg_type, f"Unknown({msg_type})"),
        "length": length,
        "value_hex": value.hex(":") if value else "",
    }


def parse_nas_message(data):
    """Parse NAS TLV: [msg_type] [len_lo] [len_hi] [value...]"""
    if len(data) < 3:
        return None
    msg_type = data[0]
    length = data[1] | (data[2] << 8)
    value = data[3:3 + length]
    type_names = {1: "AttachRequest", 2: "AttachAccept", 3: "AttachReject", 4: "Detach"}
    result = {
        "layer": "NAS",
        "msg_type": msg_type,
        "msg_name": type_names.get(msg_type, f"Unknown({msg_type})"),
        "length": length,
    }
    if msg_type == 1 and value:
        result["imsi"] = bytes(value).decode("ascii", errors="replace")
    elif msg_type == 2 and len(value) >= 4:
        result["tmsi"] = value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24)
    return result


def analyze_pdu(hex_str):
    """Analyze a hex-encoded PDU through all layers."""
    data = bytes.fromhex(hex_str.replace(":", ""))
    layers = []

    mac_info, _ = parse_mac_subheader(list(data))
    if not mac_info:
        return [{"error": "Failed to parse MAC subheader"}]
    layers.append(mac_info)

    payload = mac_info.get("payload_bytes", [])
    if not payload:
        return layers

    pdcp_info, _ = parse_pdcp_header(list(payload))
    if pdcp_info:
        layers.append(pdcp_info)
        inner = pdcp_info.get("payload_bytes", [])
    else:
        inner = list(payload)

    if inner:
        rrc_info = parse_rrc_message(inner)
        if rrc_info:
            layers.append(rrc_info)
        else:
            nas_info = parse_nas_message(inner)
            if nas_info:
                layers.append(nas_info)

    return layers


def format_layers(layers):
    lines = []
    for l in layers:
        layer = l.get("layer", "?")
        if layer == "MAC":
            lines.append(f"  MAC: LCID={l['lcid']} F={l['f']} Len={l['length']}")
        elif layer == "PDCP":
            lines.append(f"  PDCP: ver={l['version']} seq={l['seq_num']}")
        elif layer == "RRC":
            lines.append(f"  RRC: {l['msg_name']} len={l['length']}")
        elif layer == "NAS":
            extra = ""
            if "imsi" in l:
                extra = f" IMSI={l['imsi']}"
            elif "tmsi" in l:
                extra = f" TMSI=0x{l['tmsi']:08X}"
            lines.append(f"  NAS: {l['msg_name']} len={l['length']}{extra}")
        elif "error" in l:
            lines.append(f"  ERROR: {l['error']}")
    return lines


def main():
    parser = argparse.ArgumentParser(description="Analyze AetherStack PDU traces")
    parser.add_argument("--input", "-i", default=None, help="Input log file")
    args = parser.parse_args()

    if args.input:
        with open(args.input) as f:
            raw_lines = f.readlines()
    else:
        raw_lines = sys.stdin.readlines()

    for line in raw_lines:
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue

        event = entry.get("event", "")
        if event != "PDU_TRACE":
            continue

        fields = entry.get("fields", {})
        direction = fields.get("direction", "?")
        layer = fields.get("layer", "?")
        length = fields.get("length", 0)
        hex_data = fields.get("hex", "")

        print(f"[{direction}] {layer} ({length} bytes)")
        if hex_data:
            layers = analyze_pdu(hex_data)
            for l in format_layers(layers):
                print(l)
        print()


if __name__ == "__main__":
    main()
