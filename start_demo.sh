#!/usr/bin/env bash
# M8: AetherStack one-command launcher.
#
#   ./start_demo.sh                 interactive (drive UE via stdin commands)
#   ./start_demo.sh --with-demo     unattended scripted run (M8.2 scenario)
#   ./start_demo.sh --loss-rate 0.1 heavier channel impairment
#
# Components: log server (UDP 9999 <-> WS 8765), channel simulator
# (D3 topology: UL 11001 -> BS, DL 21002 -> UE), Web LMT (:3000),
# BS and UE nodes. Ctrl+C tears everything down.
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

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-demo) WITH_DEMO=1; shift ;;
        --loss-rate)
            LOSS_RATE="$2"; shift 2 ;;
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

# 4. Channel simulator (D3 topology)
echo -e "${YELLOW}[*] Starting channel simulator (loss ${LOSS_RATE})...${RESET}"
"$VENV_PYTHON" tools/channel/sim_channel.py --loss-rate "$LOSS_RATE" &
PIDS+=($!)
sleep 0.5

# 5. Web LMT
echo -e "${YELLOW}[*] Starting Web LMT (http://localhost:3000)...${RESET}"
(cd lmt && npm install --silent 2>/dev/null || true)
(cd lmt && npm run dev -- --no-clearScreen) &
PIDS+=($!)
sleep 2

# 6. C++ nodes routed through the channel (D3)
echo -e "${YELLOW}[*] Starting BS node (downlink via channel 21002)...${RESET}"
./build/bin/bs --log-host 127.0.0.1 --log-port 9999 --ue-phy-port 21002 &
PIDS+=($!)
wait_port 20002 "BS PHY"

echo -e "${YELLOW}[*] Starting UE node (uplink via channel 11001)...${RESET}"
./build/bin/ue --log-host 127.0.0.1 --log-port 9999 --bs-phy-port 11001 &
PIDS+=($!)
wait_port 10001 "UE PHY"

echo -e "\n${GREEN}[+] AetherStack is running!${RESET}"
echo -e "${GREEN}    Web LMT : http://localhost:3000${RESET}"
if [ "$WITH_DEMO" = "1" ]; then
    echo -e "${GREEN}    Demo    : unattended scenario starting (watch the banner)${RESET}\n"
    sleep 1.5
    "$VENV_PYTHON" tools/demo/demo_scenario.py
    DEMO_RC=$?
    echo -e "${GREEN}[+] Scenario finished (rc=$DEMO_RC). Press Ctrl+C to stop.${RESET}"
else
    echo -e "${GREEN}    UE stdin: attach | detach | send <text> | traffic | status${RESET}"
fi

echo -e "${GREEN}    Press Ctrl+C to stop.${RESET}\n"
wait
