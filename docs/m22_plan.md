# M22 — Dual-BS mobility with handover

Status: implemented (P8, capstone). Builds on M14 (measurement reports,
handover, paging, RLF re-establishment in-process) and takes the same
machinery live across two gNB processes.

## Topology (live demo)

```
        UL 11001                DL 21002 (cell 1) / 21003 (cell 2)
 UE(s) ──► channel sim ──► both BS PHYs      BS1 ──┐      ┌── BS2
       ◄── channel sim ◄── per-cell fan-out  20002 │ Xn   │ 20003
                        (per-(UE,cell) links)      └── UDP ──┘
                                              20201 ⇄ 20202
```

* Two `bs` processes: BS1 (`--cell-id 1 --pci 0 --crnti-base 1`, PHY 20002,
  cmd 10102) and BS2 (`--cell-id 2 --pci 1 --crnti-base 16385`, PHY 20003,
  cmd 10106). Cell-scoped C-RNTI ranges ([base,+0x1000) RACH,
  [base+0x1000,+0x2000) HO) keep identifiers unambiguous on the shared
  medium (`BsNodeConfig.crnti_base`, wired to `RachBs`/`RrcBs`/
  `next_ho_crnti_` in the `BsNode` ctor). Logs are tagged `bs1`/`bs2`.
* Channel sim (`tools/channel/sim_channel.py`): UL ingress fans out to both
  BS PHY ports (per-destination impair), one DL ingress per cell
  (21002/21003) fanning out to all UEs. Per-(UE,cell) quality matrix
  (`--cell-quality 10001=1:good,10001=2:poor,...`); the legacy
  `--ue-quality` maps onto cell 1 unchanged. New profile "bad"
  (15 % loss / 15 dB): the serving cell effectively goes dark for one UE.
* **Mobility driver**: UDP control port 11009 — `move <ue_port> <cell>`
  flips that UE's link to `good` on the target cell and `bad` on the
  other (`move_controller`). start_demo.sh seeds cell1=good / cell2=poor
  so every UE camps on BS1 at boot.

## Cell selection & isolation on the shared medium

* **Preamble partitioning** (`mac/rach_common.h:preamble_for_cell`): the
  RACH preamble space is halved per cell (cell 1: 0-31, cell 2: 32-63);
  `RachBs` answers only its own half. Both cells hear every MSG1, but only
  the targeted cell answers — no RAR/MSG4 races.
* **Setup gate**: RRC SetupRequest carries the target cell id
  (`RrcUe::start_connection(cell_id)`); the foreign cell stays silent
  (`RrcBs` gate). RESUME_REQUESTs are gated by the resume-id's C-RNTI
  ownership range so the non-owner can't fire a racing RESUME_FAILURE.
* **UE camping**: `UeNode::select_serving_cell` picks the strongest
  audible cell (SIB rx_count) before RACH and targets the preamble at it.
* **PCI at PHY**: `phy_rx_frame` accepts a wildcard PCI (cfg.pci < 0) on
  the UE — it must hear both cells (demux by SIB1 cell_id / RNTI above);
  UL preambles carry the SERVING cell's PCI (convention pci == cell_id-1,
  matching the M14 test fixtures), so each BS only decodes its own UEs'
  uplink physically.

## Handover path: real Xn handover (not re-establishment)

Chosen path = **coordinated handover over a direct Xn UDP link** between
the gNBs (`BsNode::attach_xn`, reusing the M15 `CnLink`/`UdpCnLink`
carrier with new MsgTypes 40-43). Re-establishment (M14 RLF fallback) was
NOT needed: the Xn prepare/ack flow slots into the existing M14
`request_handover`/`prepare_handover`/`HoContext` machinery with no UE
protocol changes.

1. UE's meas report lacks the serving cell (it went `bad`); the serving
   BS triggers (`bs_node.cpp` MEAS_REPORT case, now also `|| xn_`).
2. `HANDOVER_START {imsi, from, to}` + `XN_HO_PREPARE` {tmsi, from, to,
   sec, key, imsi} to the peer (`request_handover` Xn path; pending state
   is recorded BEFORE send — in-memory carriers deliver synchronously).
3. Target `prepare_handover` (fresh C-RNTI, keys, TMSI/NAS adoption) →
   `XN_HO_PREPARE_ACK` → source sends RRC HO_COMMAND to the UE.
4. UE `apply_handover` (M14, unchanged): adopts the new cell/C-RNTI, keeps
   NAS registration, security and SIP dialogs; bearers reset, HO_COMPLETE.
5. Target sees HO_COMPLETE → `HANDOVER_DONE {imsi, from, to, path:"ho"}`
   → `XN_HO_COMPLETE` to the source → source releases its context
   (`release_ho_source`; the flow erase is deferred to the next tick
   because the in-memory Xn delivers reentrantly inside
   schedule_downlink's flows_ iteration — found by segfault in tests).
6. **Cross-cell U2U**: a UE whose peer moved away is unreachable locally —
   the SDU goes over `XN_FWD_DATA` to the peer, which delivers it into its
   own DL flow (`forward_u2u_dl` factored out of `handle_ul_app_sdu`).
   This is what keeps voice/SIP/msg flowing while the parties sit on
   different cells.

Duplicate triggers are suppressed (one handover in flight per UE).

## Events (frontend contract — matched exactly)

* `HANDOVER_START {imsi, from, to}` — serving BS on trigger.
* `HANDOVER_DONE {imsi, from, to, path}` — target on HO_COMPLETE;
  path is always "ho" in this phase ("reest" reserved for the fallback).
* Reused: `MEAS_REPORT_TX`, `LINK_QUALITY`, the M14 `HO_*` family.
* Debug-level additions: `RACH_PREAMBLE_FOREIGN_CELL`,
  `RRC_SETUP_FOREIGN_CELL` (cell-isolation traces).

## Commands / flags

`bs --cell-id --pci --crnti-base --xn-local --xn-peer --local-phy-port`;
channel sim `--cell-quality`, `--move-port`; log_server CMD_PORTS gains
`bs1`/`bs2`. Single-BS operation is unaffected: no Xn flags → no peer
required; the channel sim with single-port lists behaves exactly as before.

## Tests

In-process (`stack/tests/test_e2e_nodes.cpp`):
`CellTargetedAttachStaysOnSelectedCell` (camping + setup gate; foreign
cell holds no context), `XnHandoverMovesUeAndKeepsUserPlane` (cell goes
dark for one UE only → Xn HO → registration/keys carried → call media
keeps flowing cross-cell → cross-cell text via XN_FWD_DATA). MAC-layer
tests updated with comments for the partitioned preamble space (default
preamble moved to cell 1's half). Full suite 179/179 green, event catalog
137 entries in sync.

## Live evidence (dual-BS demo)

3 UEs attach on BS1 (gradient camping) → ue1 voice-calls ue2 →
`move 10001 2`: HANDOVER_START {890,1,2} → HANDOVER_DONE {890,1,2,ho},
ue1 lands on c_rnti 20481 (BS2 HO range) → `move 10001 1`:
HANDOVER_START {890,2,1} → HANDOVER_DONE {890,2,1,ho}, ue1 back on
c_rnti 4097. Across BOTH handovers: ue1 out-stream tx 3043 / acks 3038 /
loss 0 / RTT 274 ms; ue2 in-stream rx 2842 with ~6.6 % loss confined to
the transition windows. `--with-demo` PASS rc=0 unchanged.

## Simplifications / deviations

* pci == cell_id-1 by deployment convention (PHY preamble ↔ SIB1 cell_id
  binding; no separate PCI signalling in SIB1).
* Handover trigger keeps the M14 policy (serving must go STALE/missing),
  driven by the "bad" profile rather than a dB-hysteresis comparison.
* Xn is a single direct UDP link (two-cell deployment); no path-switch
  signalling to a core (the embedded NAS/HSS is replicated per cell via
  the same `--subscriber` list).
* The losing side of a RACH on the shared medium can leave a transient
  CONNECTING context (no completion) — reaped by the inactivity/expiry GC.
* The M14 AMF-arbitrated and in-process coordinator paths are untouched;
  HANDOVER_START/DONE are emitted on the Xn and coordinator paths.
