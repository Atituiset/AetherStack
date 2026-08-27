# M18 — 3-party conference call (audio bridge at the BS)

Status: implemented (P4). Builds on M16 (U2U user plane), M17 (SIP-lite
dialogs + QoS dedicated bearers).

## Goal

A host UE pulls two other UEs into a conference: the BS app layer acts as
an audio bridge and fans every participant's voice media out to all the
others. Demo story: UE1 发起多方通话，UE2、UE3 先后振铃加入，三方互相都能
"听到"（各自的 rx 计数增长），任一方可离开，主持人 `conf end` 结束会议。

## Signaling: conference-flavoured SIP-lite

No parallel protocol — the existing dialog machinery carries a conf_id:

* Sig payload gains a forward-tolerant trailing field:
  `[method:1][call_id:4 LE][media_kind:1][conf_id:4 LE]` (10 bytes; older
  peers send 6 and decode as conf_id=0 — `stack/app/src/u2u.cpp`).
* `conf <imsiB> <imsiC>` on the host (`UeNode::start_conf`,
  `stack/core/src/ue_node.cpp:799`) creates one ordinary OUTGOING_RINGING
  dialog per party, both kind voice and sharing a conf_id
  (`(host_c_rnti << 16) | seq` — unique cell-wide without coordination).
  Parties `answer`/`decline` as usual (auto-answer applies); each dialog is
  an independent INVITE/180/200/ACK exchange. Ring timeouts and declines
  fail only their own dialog.
* Interop: the occupied rule already 486s any INVITE while a dialog exists
  (and `start_conf`/`start_call` fail locally "busy" when dialogs are
  active), so conference and 1:1 calls mutually exclude.
* The BS splices membership by **snooping the signaling it forwards**
  (`BsNode::snoop_conf_sig`, `stack/core/src/bs_node.cpp:747`): first
  conf-carrying INVITE opens the conference (CONF_START, host = src), each
  200 OK joins the party (CONF_JOIN), BYE/486/603/CANCEL resolves a member
  or invitation (CONF_LEAVE). The conference closes when the host leaves
  ("host"), membership drops below two after being multi-party ("empty"),
  or every invitation was refused ("no-parties"). Detach purges stale
  members (`purge_conf_member`) as a backstop for lost BYEs.

## Media: BS bridge fan-out

* Every participant (host included) sends ONE voice stream addressed to
  the bridge (`ensure_conf_media`, `ue_node.cpp:1097`): empty dst_imsi,
  conf_id carried as a new CONF flag (bit5 of the u2u kind byte) plus a
  `[conf_id:4 LE]` field between dst and payload. Media rides the QCI1
  voice bearer end-to-end.
* The BS bridge (`BsNode::bridge_conf_media`, `bs_node.cpp:718`) re-encodes
  one copy per receiver (dst = receiver IMSI, src/seq/conf_id preserved)
  into each member's downlink flow — simple fan-out, no mixing (payloads
  are synthetic). Each party therefore keeps a per-sender in-stream and its
  `APP_STREAM_STATS` rx grows from BOTH other parties.
* Pacing: bridge fan-out doubles per-flow DL load (each downlink carries
  two streams), so conference legs run at HALF the 1:1 voice rate (60 ms
  instead of 30 ms). At full rate the three flows saturated their HARQ
  budget and shed ~50% of media; at half rate a live 3-party conference
  measures out-stream loss 0 / RTT ~305 ms and per-sender in-stream loss
  ~10% (same as a 1:1 voice call at 5% channel loss).
* Ack model: receivers ack each media packet to its original sender
  (unicast, conf_id echoed). The sender's single conference out-stream
  takes the first ack per seq for RTT and treats any later ack as proof of
  delivery (slightly optimistic under asymmetric loss — documented).
* `conf end` (host) BYEs every conference dialog (`UeNode::end_conf`,
  deque-safe re-scan loop); a participant's `call end` BYEs only its own
  dialog = leave. Stream teardown is conference-aware in `erase_dialog`:
  the shared out-stream dies with the last dialog of the conference,
  per-sender in-streams die with their dialog peer (or all at once when
  the local conference ends).

## Events

New (`events.h` + `lmt/src/events.ts` mirror, all string:string):
`CONF_START {host, conf_id}`, `CONF_JOIN {conf_id, imsi}`,
`CONF_LEAVE {conf_id, imsi, reason}` with
`reason ∈ hangup|busy|decline|cancel|detach`,
`CONF_END {conf_id, reason}` with `reason ∈ host|empty|no-parties`.

`APP_STREAM_STATS`: conference streams report `kind="conf"` (still `qci="1"`)
plus a `conf_id` field; the out-stream's peer label is "conf", in-streams
keep the sender IMSI as peer. `APP_FORWARD` logs bridge fan-out with
`kind="conf"` (aggregated 1 s windows as usual).

## Commands

`conf <imsiB> <imsiC>` — host starts a conference;
`conf end` — host tears it down; participants leave with `call end`.
`answer`/`decline`/`autoanswer` unchanged. `msg`, `video`, legacy `call`
and loopback untouched.

## Limits / simplifications

* Conference size is bounded by the deployment (3 UEs); `start_conf` takes
  exactly two parties (the constant lives in the command shell, the core
  logic is N-party).
* One conference per cell at a time in practice — a UE in any dialog 486s
  further INVITEs, and conf_id collisions are impossible (host C-RNTI in
  the high half).
* Handover mid-conference is not modelled (membership follows the IMSI; a
  re-registered UE is a new member only via a fresh INVITE).

## Tests

In-process (`stack/tests/test_e2e_nodes.cpp`):
`ConfThreePartyBridgeFlow` (host starts, both join, every party receives
from BOTH others, ue3 leaves and the remaining two continue, host ends →
all torn down), `ConfDeclineKeepsTwoPartyConference` (decline → 2-party
conference proceeds), `ConfInviteToBusyUeGets486` (conf INVITE to UEs in a
1:1 call → 486 ×2, BS closes the empty conference, original call
undisturbed). All pre-existing suites unchanged and green (164/164, one
FEC benchmark disabled).
