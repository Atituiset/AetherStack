# AetherStack 📡

A wireless communication system MVP built with a **layered skill-driven** approach. Target: a simulated UE and gNB on a single machine, observable end-to-end.

> ⚠️ **Current Status: M0 Skeleton Only**
> The protocol stack is **not yet implemented**. What exists today is the engineering skeleton: build system, structured logging, simulation channel, and a Web LMT dashboard. Real protocol logic (PHY, MAC, RLC, PDCP, RRC, NAS) will be added incrementally following the skill cards in `.skills/`.

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────┐
│  Web LMT (TypeScript + React)           │
│  Real-time log stream, node status      │
│  http://localhost:3000                  │
└──────────────┬──────────────────────────┘
               │ WebSocket :8765
┌──────────────▼──────────────────────────┐
│  Python Tooling Layer                   │
│  log_server.py  – UDP → WebSocket relay │
│  sim_channel.py – UDP relay + loss/lat  │
└──────────────┬──────────────────────────┘
               │ UDP :9999 (logs) / :10001-10002 (data)
┌──────────────▼──────────────────────────┐
│  C++ Protocol Stack (skeleton)          │
│  UE / BS processes with structured JSON │
│  logging over UDP. No protocol logic yet│
└─────────────────────────────────────────┘
```

---

## 📁 Directory Structure

```
.
├── CMakeLists.txt              # Top-level build
├── Makefile                    # Convenience wrapper
├── start_demo.sh               # One-command orchestrator
├── README.md                   # This file
├── docs/                       # DeepSeek design conversations
│   ├── deepseek-1.md .. 5.md
├── .skills/                    # Skill cards for agent-driven dev
│   ├── m0_1_project_structure.md
│   ├── m0_2_unified_logger.md
│   └── m0_4_web_lmt_skeleton.md
├── stack/                      # C++ protocol stack
│   ├── common/                 # Shared logger library
│   ├── ue/                     # UE executable skeleton
│   ├── bs/                     # BS executable skeleton
│   └── tests/                  # Google Test suite
├── tools/                      # Python tooling
│   ├── log_server/
│   └── channel/
└── lmt/                        # TypeScript Web frontend
    └── src/
```

---

## 🚀 Quick Start

### One-command run

```bash
./start_demo.sh
```

Then open **http://localhost:3000** in your browser.

### Manual steps

Build C++ stack:
```bash
make
```

Run tests:
```bash
make test
```

Start Python services:
```bash
python3 tools/log_server/log_server.py &
python3 tools/channel/sim_channel.py &
```

Start Web LMT:
```bash
cd lmt && npm install && npm run dev
```

Run C++ nodes:
```bash
./build/bin/bs --log-host 127.0.0.1 --log-port 9999
./build/bin/ue --log-host 127.0.0.1 --log-port 9999
```

---

## 🧪 What's Working (M0)

| Component | Status | Notes |
|-----------|--------|-------|
| C++ build (CMake) | ✅ | `make` produces `ue` and `bs` |
| Structured JSON logger | ✅ | Thread-safe, UDP output, no deps |
| Google Test framework | ✅ | Fetched automatically by CMake |
| Python log server | ✅ | UDP 9999 → WebSocket 8765 |
| Python channel sim | ✅ | UDP relay + configurable loss/latency |
| Web LMT | ✅ | Log stream, filters, mock data, auto-reconnect |

---

## 📋 What's NOT Working Yet

- ❌ Physical layer (QPSK, OFDM, sync)
- ❌ MAC layer (RACH, scheduling, HARQ)
- ❌ RLC / PDCP / RRC / NAS
- ❌ Actual PDU encoding/decoding
- ❌ Real UE-BS handshake

These will be built incrementally following the skill cards. See `.skills/` and `docs/` for the roadmap.

---

## 🛠️ Development Approach

This project is being built with a **skill-card-driven** methodology:
1. Each skill card defines one atomic, testable unit of work.
2. The agent reads the card, implements, tests, and reports.
3. You review and approve before moving to the next card.

See `docs/deepseek-3.md` for the full 8-phase roadmap.
