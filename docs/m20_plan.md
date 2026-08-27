# M20 — RRC Inactive state + fast Resume (NR-style RRC_INACTIVE)

Status: implemented (P6). Builds on M4 (RRC FSMs), M14 (paging,
re-establishment pattern), M12 (kept security context).

## Goal

After an inactivity period the connected UE drops to RRC_INACTIVE —
context (C-RNTI binding, security keys, RLC bearer state, NAS
registration) kept on BOTH sides — and the next activity triggers a
lightweight Resume (RACH + RRCResumeRequest) instead of a full
RACH + RRC setup + NAS attach. Demo story: 锁屏（inactive）后点亮屏幕，
信令开销约为完整 attach 的一半，通话能力立刻恢复。

## Suspend (entering INACTIVE)

* **Trigger 1 — inactivity timer** (BS, `BsNodeConfig.inactive_ms`, default
  0 = off in code, 15 s in `start_demo.sh`; `--inactive-ms` flag).
  `BsNode::sweep_inactive` (`bs_node.cpp:288`) suspends any connected flow
  whose last USER-PLANE uplink is older than the timer. Only NAS + the four
  app LCIDs count as activity — MEAS_REPORTs, CQI CEs and HARQ-ACKs are
  background chatter and would pin an idle UE connected forever. An active
  SIP call or media stream therefore never suspends (media IS activity).
* **Trigger 2 — UE request**: the `sleep` command sends RRC RELEASE with
  flags bit1 (`RrcUe::request_suspend`); the BS runs the same suspend
  handshake.
* **Handshake** (`RrcBs::suspend_context`, `rrc_bs.cpp`): the context
  transitions CONNECTED→INACTIVE with a freshly allocated resume identity
  (`(c_rnti << 16) | seq`) and the UE is sent RRC RELEASE
  `[crnti:2][flags=0x1][resume_id:4]`. The BS parks the flow's data path
  (HARQ pipes reset, `DlFlow.suspended`) but KEEPS keys/bearer queues; the
  UE transitions to `UeState::INACTIVE` (`rrc_ue.cpp`), resets HARQ and
  gates uplink (`pump_app_bearers` early-return) while keeping bearers,
  dialogs, keys and NAS REGISTERED.
* The suspend RELEASE itself rides the control queue, which keeps draining
  for suspended flows (app bearers stay parked) and HARQ retransmissions
  keep running — a lost suspend RELEASE would otherwise desync the two
  sides (found live). Backstop: real uplink content from a suspended UE
  re-activates its context (`RrcBs::reactivate_context`); a bare HARQ-ACK
  does NOT un-park (the UE acks the suspend RELEASE at MAC level).
* Context expiry: a flow suspended longer than 5× the timer is
  garbage-collected; a late resume then gets RESUME_FAILURE and falls back
  to full attach (NR-like).

## Resume

* UE side (`UeNode::wake`, `ue_node.cpp`): RACH with
  `RESUME_REQUEST [resume_id:4]` as the MSG3 CCCH payload (same path as a
  SetupRequest). Started by the `wake` command, implicitly by outbound
  activity (`send`/`msg`/`call`/`conf`/`traffic` — SDUs queue on their
  bearers and flush after the resume), by paging (see below), or by
  `detach` from INACTIVE (resume first, then a normal connected detach so
  the network context is torn down).
* BS side (`RrcBs` RESUME_REQUEST case, `rrc_bs.cpp`): the identity is
  validated against the suspended contexts; on match the context migrates
  to the fresh MSG3-assigned C-RNTI (the M14 re-establishment callback
  moves flow/TMSI/security with it — queues and keys intact) and
  `RESUME_OK [new_crnti:2]` goes back. Unknown/stale identity →
  `RESUME_FAILURE` → UE re-attaches fully (RACH + RRC setup + NAS).
* The resume guard is 4× the attach guard (12 s) — 3 s proved too tight at
  5 % loss: a lost RESUME_OK made the UE fall back to a full attach and
  orphaned the migrated BS context (found live).
* **Cost** (tick-driven, 30 ms/hop delayed air, e2e test): attach
  = RACH(4) + RRC setup(3) + NAS(~6 hops) ≈ **210 ms**; resume
  = RACH(4) + RESUME_REQUEST/OK(2) ≈ **120 ms** — ~43 % cheaper, asserted
  in `RrcResumeIsCheaperThanAttach`.

## Paging for downlink to an inactive UE

DL SDUs for a suspended flow keep queueing on its bearers (they are NOT
dropped); the first one triggers the existing M14 paging record
(`page(imsi)` → LCID_PAGING in the SIB burst, `PAGE_TX`). The UE wakes on
its identity (`PAGE_RX` → `wake()` instead of the idle-UE full attach),
resumes, and the parked DL drains — an incoming SIP INVITE is delivered
without waiting for AM retransmission. Conference media pages suspended
members the same way.

## Events

New (`events.h` + `lmt/src/events.ts` mirror):
`RRC_INACTIVE {c_rnti, resume_id}` (UE+BS),
`RRC_RESUME_REQUEST {resume_id}` (UE TX) / `{resume_id, c_rnti}` (BS RX),
`RRC_RESUMED {c_rnti, old_c_rnti}` (UE+BS; `via:"uplink"` on the
desync-recovery path), `RRC_RESUME_FAIL {resume_id, reason}` (BS) /
`{reason}` (UE). `PAGE_TX`/`PAGE_RX` reused (M14). `RRC_UE_STATE`
transitions show INACTIVE via `ue_state_str`.

## Commands

`sleep` — request suspend (demo-friendly, no waiting for the timer);
`wake` — explicit resume; outbound activity resumes implicitly;
`status` shows `rrc:3` while INACTIVE. `start_demo.sh --inactive-ms <ms>`
(default 15000).

## Tests

In-process (`stack/tests/test_e2e_nodes.cpp`, `InactiveCell` fixture with
optional delayed air): `RrcInactiveSuspendResumePreservesSession`
(suspend → msg resumes + delivered → voice call works right after,
registration/bearers kept), `RrcResumeIsCheaperThanAttach` (120 vs 210
ms), `RrcResumeStaleIdFallsBackToFullSetup` (RESUME_FAILURE → full
re-attach → loopback works), `PagingWakesInactiveUeOnIncomingCall`
(INVITE → page → resume → ringing → answer → media),
`DetachFromInactiveWorks` (resume-then-detach, BS context gone).
Full suite 172/172 green, event catalog 133 entries in sync.

## Simplifications / deviations

* The resume identity is a plain (c_rnti,seq) token — no MAC-I-style
  cryptographic resume token (the kept PDCP security context provides the
  real protection; documented simplification).
* The inactivity timer lives only on the BS (NR also has UE-side
  preferences); UE-initiated suspend is the explicit `sleep` handshake.
* DL SDUs that arrive between the suspend and the resume stay queued —
  first packet is not forwarded at paging time (NR behaviour would be
  identical for the control plane; small data in paging is not modelled).
* A UE suspended mid SIP-signalling (e.g. waiting out a ring timeout in
  silence) can be suspended — media activity is the guard, per the design;
  the dialog survives because AM retransmits after the resume.
