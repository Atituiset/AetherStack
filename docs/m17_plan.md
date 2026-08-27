# M17 — QoS-differentiated dedicated bearers

Status: implemented (P3). Builds on M16 (UE-to-UE media) and M16/M17
(SIP-lite call signaling).

## Goal

LTE-style QoS: separate radio bearers per service class with priority
scheduling, so latency-sensitive traffic (signaling, voice) stays smooth
while bulk traffic (video) absorbs congestion. Demo story: 3 UEs online,
ue1↔ue2 video + ue1↔ue3 voice simultaneously — voice keeps low RTT/loss,
video takes the hit.

## Bearer model

| class | QCI | LCID (data) | LCID (AM STATUS) | RLC | content |
|-------|-----|-------------|------------------|-----|---------|
| sig   | 5   | `LCID_APP_SIG` (3)   | `LCID_RLC_STATUS_SIG` (57)   | AM | SIP-lite call control |
| voice | 1   | `LCID_APP_VOICE` (4) | — (UM) | **UM** | conversational voice |
| video | 2   | `LCID_APP_VIDEO` (5) | — (UM) | **UM** | conversational video |
| best effort | 9 | `LCID_APP_DTCH` (2) | `LCID_RLC_STATUS` (59) | AM | msg one-shots + legacy loopback (default bearer) |

Each bearer has its own RLC entity and its own queue on both the UE
(`UeNode::bearers_`) and every BS downlink flow (`DlFlow::bearers`), see
`stack/core/include/core/qos.h`. The BS mirrors the service class into the
peer UE's downlink bearer by reusing the same LCID when forwarding.

Simplifications (deliberate):
* **UM on media bearers, AM on sig/best-effort.** The first cut used AM
  everywhere; under a real video flood the AM machinery amplified the
  overload (STATUS + ARQ retransmissions of doomed media) and voice
  drowned with video. UM was extended with AM-style FI segmentation
  (video SDUs exceed one air frame) and a far-ahead resync (a blackout
  longer than the reorder window re-anchors instead of dropping the
  stream). Media loss now surfaces as plain sequence gaps; the flood
  keeps a constant offered rate and priority scheduling actually
  differentiates. Bearer queues are capped (64) with shed-oldest, so a
  congested pipe drops stale media instead of delaying it.
* **One PDCP COUNT space per direction** shared across bearers (the nonce
  stays unique; per-bearer COUNT is not modelled). The security decision
  is captured at enqueue time (`AppPdu.cipher`) so the ATTACH_ACCEPT
  still goes out in the clear; ciphering itself happens at drain time so
  the COUNT matches air order.
* **Implicit bearer setup** — the first SDU of a class instantiates the
  bearer (no NAS/RRC bearer-activation procedure). UE tears a class down
  when its last dialog of that class ends; BS bearers live for the flow's
  lifetime and are logged down at flow erase (detach / handover release).

## Scheduling

`core::BearerSet` (qos.h): one control queue (CCCH/NAS/RLC STATUS) served
first, then **strict priority** sig > voice > video > best-effort with a
**min-share guard**: whenever best-effort has data, at least every 4th
pick is served from it (25% floor) so it cannot starve under full load.
Justification: strict priority is what makes voice latency provably
immune to a video flood; DWRR would smooth video at the cost of voice
tail latency. The 25% floor covers the legacy loopback and texts.

Applied identically on the UE uplink (`UeNode::pump_app_bearers`, one
shared HARQ pipe) and per BS downlink flow (`BsNode::schedule_downlink`,
flows still round-robin between UEs). UL RLC control (probes, STATUS,
retx) rides the same bearer's queue; connection control (RRC/NAS) jumps
the control queue.

## Events

New (events.h + lmt/src/events.ts mirror):
`QOS_BEARER_SETUP {c_rnti, qci, kind}`, `QOS_BEARER_TEARDOWN {c_rnti, qci,
kind}` (kind ∈ "sig"|"voice"|"video"; qci is the decimal QCI string).
`APP_STREAM_STATS` gained `qci` (kept `kind`) — emitted per active stream
on both UEs.

## Multi-dialog calls (enabler for the demo story)

The SIP dialog state moved from a single `dialog_` to
`std::deque<CallDialog> dialogs_`; media streams are per (peer, kind)
(`out_streams_`/`in_streams_` vectors). A UE may hold concurrent dialogs
of DIFFERENT kinds (voice + video); a second dialog of the same kind is
refused locally ("busy"). Callees still 486 any second incoming INVITE.
`call end` / `video end` hang up the dialog of that kind.

## Tests

`QosBearerSetPriorityAndMinShare` (drain order + 25% BE floor),
`QosBearerSetupAndTeardownEvents` (UE + BS setup, UE teardown on hangup,
BS teardown at flow erase), `QosConcurrentVoiceAndVideoCallsDifferentiate`
(concurrent calls, per-kind bearers, same-kind busy, BE loopback not
starved, kind-specific hangup), `QosVoiceProtectedWhenVideoSaturatesPipe`
(priority under contention: 70 ms-delayed air overbooks the 8-process HARQ
pipe ~1.8x with video+voice; voice stays lossless at 276 ms avg RTT while
video sheds 95 SDUs at 580 ms; BE ping still answered mid-flood). All
pre-existing suites unchanged and green (161/161, one FEC benchmark
disabled).
