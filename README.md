# AetherStack

A 5G-NR-inspired **software protocol stack** written from scratch in C++17 —
PHY to user plane — with a live UDP/OFDM air interface, a channel simulator,
and a Web-based Local Maintenance Terminal (LMT).

```
 ┌────────┐  IQ/UDP   ┌───────────────┐  IQ/UDP   ┌────────┐
 │   UE   ├──────────►│    Channel    ├──────────►│   BS   │
 │ (C++)  │◄──────────│  Simulator    │◄──────────│ (C++)  │
 └───┬────┘           │ loss·blackout │           └───┬────┘
     │                └───────────────┘               │
     │            structured JSON logs (UDP 9999)     │
     └──────────────────────┬─────────────────────────┘
                            ▼
                     [ Log Server ] ──WebSocket 8765──► Web LMT (:3000)
```

## Protocol stack

| Layer | What it does |
|-------|--------------|
| PHY | QPSK modulation + OFDM (Cooley-Tukey FFT, CP), IQ over UDP |
| MAC | PDU mux/demux (LCID), full 4-step RACH contention procedure |
| RLC / PDCP | Transparent-mode relay, transparent PDCP |
| RRC | Connection setup/release state machines, MIB/SIB1 broadcast |
| NAS | Attach request/accept, detach, TMSI assignment |
| App | User-plane ping-pong with sequence numbers and RTT accounting |

**Orchestration layer** (`stack/core`): `UeNode` / `BsNode` own all layer
entities; process mains are thin shells (socket poll + timer tick +
command channel). Everything is tick-driven — fully deterministic in tests.

## Quick start

```bash
git clone <repo> && cd AetherStack
./start_demo.sh                 # builds if needed, starts everything
# open http://localhost:3000
```

Unattended scripted demo (attach → traffic → release, narrated in the LMT
banner):

```bash
./start_demo.sh --with-demo --loss-rate 0.05
```

`Ctrl+C` tears down every component.

### Driving the UE by hand

Type commands into the UE's stdin, or send them via UDP (what the LMT and
the demo driver use):

```bash
echo attach | nc -u -w1 127.0.0.1 10101
echo "traffic on" | nc -u -w1 127.0.0.1 10101
```

Commands: `attach` `detach` `send <text>` `traffic on|off` `stats` `status`.

## Tests & verification

```bash
make test                       # 93 unit/E2E tests (gtest)
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DAETHER_SANITIZE=ON \
  && cmake --build build-asan -j && (cd build-asan && ctest)   # ASan+UBSan
python3 tools/scripts/check_events_sync.py      # event catalog C++<->TS mirror

# cross-process lifecycle smoke (direct or through the lossy channel)
python3 tools/test_scripts/e2e_smoke.py --timeout 12
python3 tools/test_scripts/e2e_smoke.py --channel --loss-rate 0.15 --timeout 20

# fault recovery: clean -> 50% loss -> blackout -> auto-resume
python3 tools/test_scripts/recovery_test.py

# long-run stability harness (default 30 min, RSS/stall/crash guards)
python3 tools/test_scripts/stability_run.py --duration 1800 \
    --channel --loss-rate 0.05 --blackout "600:10,1200:8"
```

## Milestones

| Milestone | Scope | Status |
|-----------|-------|--------|
| M0 | Skeleton: CMake, logger, channel sim, web LMT | ✅ |
| M1 | L1 closed loop: QPSK → OFDM → E2E IQ exchange | ✅ |
| M2 | MAC: PDU codec + 4-step RACH (UE+BS) | ✅ |
| M3 | RLC TM + PDCP transparent vertical passthrough | ✅ |
| M4 | RRC connection + NAS attach flow | ✅ |
| M5 | Observability + user-plane ping-pong | ✅ |
| M6 | Web LMT: topology / FSM / MSC / PDU inspector | ✅ |
| M6.5 | Cross-process end-to-end (nodes over real UDP+PHY), event catalog, command channels | ✅ |
| M7 | Sustained loopback, ASan audit, 30-min stability, fault recovery | ✅ |
| M8 | One-command demo launcher, unattended scenario, LMT demo banner | ✅ |

Design documents per milestone live in [`docs/`](docs); rendered guides in
[`docs-book/`](docs-book).

## Repository layout

```
stack/
  common/    logger (JSON+UDP), UDP transport, event catalog (events.h)
  core/      air-frame codec, deterministic timers, UeNode/BsNode orchestration
  phy/       QPSK, OFDM, IQ serialization
  mac/ rlc/ pdcp/ rrc/ nas/ app/
  ue/ bs/    thin process shells
lmt/         React + Vite local maintenance terminal
tools/
  channel/   UDP relay: loss / latency / blackout / loss schedules
  log_server/UDP->WebSocket fan-out with backpressure + command forwarding
  demo/      unattended scenario driver
  test_scripts/  smoke, stability and fault-recovery harnesses
  scripts/   MSC generator, PDU analyzer, latency report, CI checks
```

## Ports

| Port | Purpose |
|------|---------|
| 10001 / 20002 | UE / BS PHY (IQ datagrams) |
| 11001 / 21002 | Channel simulator uplink / downlink ingress |
| 10101 / 10102 | UE / BS command channels |
| 9999 / 8765 | Log server UDP in / WebSocket out |
| 3000 | Web LMT (Vite dev server) |
