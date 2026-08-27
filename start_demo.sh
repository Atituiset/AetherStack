#!/usr/bin/env bash
# M8: AetherStack one-command launcher.
#
#   ./start_demo.sh                 interactive (drive UE via stdin commands)
#   ./start_demo.sh --with-demo     unattended scripted run (M8.2 scenario)
#   ./start_demo.sh --loss-rate 0.1 heavier channel impairment
#
# Components: log server (UDP 9999 <-> WS 8765), channel simulator
# (D3 topology: UL 11001 -> BS, DL 21002 -> UEs 10001+10002), Web LMT (:3000),
# BS and two UE nodes. Ctrl+C tears everything down.
set -euo pipefail

GREEN="\033[92m"
BLUE="\033[94m"
YELLOW="\033[93m"
RED="\033[91m"
BOLD="\033[1m"
RESET="\033[0m"

PIDS=()
LOSS_RATE="0.05"
WITH_DEMO=0
UE_QUALITY=""
INACTIVE_MS="15000"  # M20: suspend idle UEs to RRC_INACTIVE after 15 s

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-demo) WITH_DEMO=1; shift ;;
        --loss-rate)
            LOSS_RATE="$2"; shift 2 ;;
        --ue-quality)
            UE_QUALITY="$2"; shift 2 ;;
        --inactive-ms)
            INACTIVE_MS="$2"; shift 2 ;;
        *)
            echo "unknown arg: $1"; exit 2 ;;
    esac
done

cleanup() {
    trap - SIGINT SIGTERM EXIT   # re-entrancy guard: run once only
    echo -e "\n${YELLOW}[*] Shutting down AetherStack...${RESET}"
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    sleep 0.5
    for pid in "${PIDS[@]:-}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    echo -e "${GREEN}[+] Done.${RESET}"
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

wait_port() { # wait_port <port> <label> [timeout_s]
    local port="$1" label="$2" timeout="${3:-15}"
    for _ in $(seq 1 "$((timeout * 10))"); do
        if python3 - "$port" <<'PY' 2>/dev/null
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    s.bind(("127.0.0.1", int(sys.argv[1])))
except OSError:
    sys.exit(1)   # port taken == someone is listening
sys.exit(0)
PY
        then :; else echo -e "${GREEN}[+] $label ready (port $port)${RESET}"; return 0; fi
        sleep 0.1
    done
    echo -e "${RED}[!] $label did not come up on port $port${RESET}"
    return 1
}

echo -e "${BOLD}${BLUE}==================================================${RESET}"
echo -e "${BOLD}${BLUE}     AetherStack Demo Orchestrator (M8)${RESET}"
echo -e "${BOLD}${BLUE}==================================================${RESET}"

# 1. Build C++ stack
if [ ! -f "build/bin/ue" ] || [ ! -f "build/bin/bs" ]; then
    echo -e "${YELLOW}[*] Building C++ stack...${RESET}"
    make
fi

# 2. Ensure Python venv exists
VENV_PYTHON=".venv/bin/python3"
if [ ! -f "$VENV_PYTHON" ]; then
    echo -e "${YELLOW}[*] Creating Python virtual environment...${RESET}"
    python3 -m venv .venv
fi
if ! "$VENV_PYTHON" -c "import websockets" 2>/dev/null; then
    echo -e "${YELLOW}[*] Installing Python deps into venv...${RESET}"
    "$VENV_PYTHON" -m pip install -q -r tools/requirements.txt
fi

# 3. Log server
echo -e "${YELLOW}[*] Starting log server (UDP 9999 <-> WS 8765)...${RESET}"
"$VENV_PYTHON" tools/log_server/log_server.py &
PIDS+=($!)
wait_port 9999 "log server"

# 4. Channel simulator (D3 topology + M22 dual-cell: UL fans out to both
# BS PHYs, one DL ingress per cell, move control on UDP 11009)
echo -e "${YELLOW}[*] Starting channel simulator (loss ${LOSS_RATE})...${RESET}"
# M22: initial per-(UE,cell) gradient so every UE camps on cell 1 at boot
# (cell 2 is audible but clearly weaker; `move` flips a UE's links).
CELL_QUALITY="10001=1:good,10002=1:good,10003=1:good,10001=2:poor,10002=2:poor,10003=2:poor"
CHANNEL_ARGS=(--loss-rate "$LOSS_RATE" --ue-dest-port 10001,10002,10003
              --bs-dest-port 20002,20003 --downlink-port 21002,21003
              --move-port 11009 --cell-quality "$CELL_QUALITY")
if [ -n "$UE_QUALITY" ]; then
    # M19: per-link quality, e.g. --ue-quality 10001=good,10002=mid,10003=poor
    echo -e "${YELLOW}    per-link quality: ${UE_QUALITY}${RESET}"
    CHANNEL_ARGS+=(--ue-quality "$UE_QUALITY")
fi
"$VENV_PYTHON" tools/channel/sim_channel.py "${CHANNEL_ARGS[@]}" &
PIDS+=($!)
sleep 0.5

# 5. Web LMT
echo -e "${YELLOW}[*] Starting Web LMT (http://localhost:3000)...${RESET}"
(cd lmt && npm install --silent 2>/dev/null || true)
(cd lmt && npm run dev -- --no-clearScreen) &
PIDS+=($!)
sleep 2

# 6. C++ nodes routed through the channel (D3)
# M21: demo subscriber keys (HSS at the BS, USIMs in the UEs) — every
# attach runs the 5G-AKA-style exchange. Demo-only keys, not secrets.
SUB1="ae7he2de10c0ffee00000000000000000aa1"
SUB2="ae7he2de10c0ffee00000000000000000aa2"
SUB3="ae7he2de10c0ffee00000000000000000aa3"

echo -e "${YELLOW}[*] Starting BS node (downlink via channel 21002)...${RESET}"
./build/bin/bs --log-host 127.0.0.1 --log-port 9999 --ue-phy-port 21002 \
    --inactive-ms "$INACTIVE_MS" \
    --cell-id 1 --pci 0 --crnti-base 1 \
    --xn-local 20201 --xn-peer 20202 \
    --subscriber 460011234567890:"$SUB1" \
    --subscriber 460011234567891:"$SUB2" \
    --subscriber 460011234567892:"$SUB3" &
PIDS+=($!)
wait_port 20002 "BS PHY"

# M22: second cell (BS2). Distinct cell id / PCI / C-RNTI space; the Xn
# link to BS1 carries handover preparation and cross-cell U2U forwarding.
echo -e "${YELLOW}[*] Starting BS2 node (cell 2, downlink via channel 21003)...${RESET}"
./build/bin/bs --log-host 127.0.0.1 --log-port 9999 \
    --local-phy-port 20003 --ue-phy-port 21003 \
    --cmd-port 10106 \
    --inactive-ms "$INACTIVE_MS" \
    --cell-id 2 --pci 1 --crnti-base 16385 \
    --xn-local 20202 --xn-peer 20201 \
    --subscriber 460011234567890:"$SUB1" \
    --subscriber 460011234567891:"$SUB2" \
    --subscriber 460011234567892:"$SUB3" &
PIDS+=($!)
wait_port 20003 "BS2 PHY"

echo -e "${YELLOW}[*] Starting UE1 node (uplink via channel 11001)...${RESET}"
./build/bin/ue --log-host 127.0.0.1 --log-port 9999 \
    --ue-id 1 --imsi 460011234567890 --usim-key "$SUB1" \
    --local-phy-port 10001 --cmd-port 10101 --bs-phy-port 11001 &
PIDS+=($!)
wait_port 10001 "UE1 PHY"

echo -e "${YELLOW}[*] Starting UE2 node (uplink via channel 11001)...${RESET}"
./build/bin/ue --log-host 127.0.0.1 --log-port 9999 \
    --ue-id 2 --imsi 460011234567891 --usim-key "$SUB2" \
    --local-phy-port 10002 --cmd-port 10103 --bs-phy-port 11001 &
PIDS+=($!)
wait_port 10002 "UE2 PHY"

echo -e "${YELLOW}[*] Starting UE3 node (uplink via channel 11001)...${RESET}"
./build/bin/ue --log-host 127.0.0.1 --log-port 9999 \
    --ue-id 3 --imsi 460011234567892 --usim-key "$SUB3" \
    --local-phy-port 10003 --cmd-port 10104 --bs-phy-port 11001 &
PIDS+=($!)
wait_port 10003 "UE3 PHY"

echo -e "\n${GREEN}[+] AetherStack is running!${RESET}"
echo -e "${GREEN}    Web LMT : http://localhost:3000${RESET}"
if [ "$WITH_DEMO" = "1" ]; then
    echo -e "${GREEN}    Demo    : unattended scenario starting (watch the banner)${RESET}\n"
    sleep 1.5
    "$VENV_PYTHON" tools/demo/demo_scenario.py
    DEMO_RC=$?
    echo -e "${GREEN}[+] Scenario finished (rc=$DEMO_RC). Press Ctrl+C to stop.${RESET}"
else
    echo -e "${GREEN}    UE1 cmd : nc -u 127.0.0.1 10101 (attach | detach | send <text> | traffic | status)${RESET}"
    echo -e "${GREEN}    UE2 cmd : nc -u 127.0.0.1 10103 (same commands)${RESET}"
    echo -e "${GREEN}    UE3 cmd : nc -u 127.0.0.1 10104 (same commands)${RESET}"
fi

echo -e "${GREEN}    Press Ctrl+C to stop.${RESET}\n"
wait
