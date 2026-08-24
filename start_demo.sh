#!/usr/bin/env bash
set -euo pipefail

GREEN="\033[92m"
BLUE="\033[94m"
YELLOW="\033[93m"
RED="\033[91m"
BOLD="\033[1m"
RESET="\033[0m"

PIDS=()

cleanup() {
    echo -e "\n${YELLOW}[*] Shutting down AetherStack...${RESET}"
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    echo -e "${GREEN}[+] Done.${RESET}"
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

echo -e "${BOLD}${BLUE}==================================================${RESET}"
echo -e "${BOLD}${BLUE}     AetherStack MVP Demo Orchestrator${RESET}"
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
if [ ! -f ".venv/lib/python3.12/site-packages/websockets" ]; then
    echo -e "${YELLOW}[*] Installing Python deps into venv...${RESET}"
    $VENV_PYTHON -m pip install -q -r tools/requirements.txt
fi

# 3. Start Log Server
echo -e "${YELLOW}[*] Starting log server (UDP 9999 <-> WS 8765)...${RESET}"
$VENV_PYTHON tools/log_server/log_server.py &
PIDS+=($!)
sleep 1

# 4. Start Channel Simulator
echo -e "${YELLOW}[*] Starting channel simulator...${RESET}"
$VENV_PYTHON tools/channel/sim_channel.py --loss-rate 0.05 --latency 0.01 &
PIDS+=($!)
sleep 1

# 5. Start Web LMT
echo -e "${YELLOW}[*] Starting Web LMT (http://localhost:3000)...${RESET}"
cd lmt
npm install --silent 2>/dev/null || true
npm run dev -- --no-clearScreen &
PIDS+=($!)
cd ..
sleep 2

# 6. Start C++ nodes (routed through the channel simulator per D3 topology)
echo -e "${YELLOW}[*] Starting BS node (downlink via channel 21002)...${RESET}"
./build/bin/bs --log-host 127.0.0.1 --log-port 9999 --ue-phy-port 21002 &
PIDS+=($!)
sleep 1

echo -e "${YELLOW}[*] Starting UE node (uplink via channel 11001)...${RESET}"
./build/bin/ue --log-host 127.0.0.1 --log-port 9999 --bs-phy-port 11001 &
PIDS+=($!)

echo -e "\n${GREEN}[+] AetherStack is running!${RESET}"
echo -e "${GREEN}    Web LMT:${RESET} http://localhost:3000"
echo -e "${GREEN}    UE commands (stdin): attach | detach | send <text> | status${RESET}"
echo -e "${GREEN}    Press Ctrl+C to stop.${RESET}\n"

wait
