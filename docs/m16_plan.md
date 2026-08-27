# M16/M17 — SIP-lite call signaling over the U2U channel

Status: implemented (P2). Builds on the M16 UE-to-UE user plane
(`stack/app/include/app/u2u.h`) and the M16.1 congestion/wedge fixes.

## Goal

Replace the M16 "call = media starts immediately" model with a real,
SIP-inspired call-control dialog so the Web LMT can show ringing and
answer/decline, while keeping unattended demos working via auto-answer.

## Wire format

Signaling rides the existing U2U header with a new `kind = SIG (3)`:

    [magic 0xA5][kind=SIG][seq][ts][src_imsi][dst_imsi] ++ sig payload
    sig payload = [method:1][call_id:4 LE][media_kind:1]   (6 bytes)

Version tolerance: receivers ignore sig payloads that are too short, carry
an unknown method, or have trailing bytes they don't understand. RLC AM
below gives reliable in-order delivery, so there are no retransmission
timers at this layer. Methods: `INVITE, 180, 200, ACK, BYE, 200(BYE),
486 Busy, 603 Decline, CANCEL`.

The BS forwards sig packets like any U2U packet; `APP_FORWARD` logs them
per-message with `kind="sig"` (signaling rates need no aggregation).

## State machines (one dialog per UE)

Caller:

    IDLE --INVITE--> OUTGOING_RINGING --180--> (ringing, got_180)
         --200 OK--> send ACK, ESTABLISHED, media starts
         --486--> SIP_CALL_FAILED reason=busy
         --603--> SIP_CALL_FAILED reason=declined
         --no 180 in 6 s--> CANCEL, SIP_CALL_FAILED reason=unreachable
         --ringing 20 s--> CANCEL, SIP_CALL_FAILED reason=timeout
         --"call end"--> CANCEL (ringing) / BYE (established)

Callee:

    IDLE --INVITE--> INCOMING_RINGING (send 180, start auto-answer 4 s)
         --"answer"/auto-answer--> send 200 OK, ESTABLISHED on ACK
         --"decline"/"call end"--> send 603, IDLE
         --CANCEL--> SIP_CALL_FAILED reason=cancel + APP_CALL_PEER_END, IDLE
         --BYE (established)--> 200(BYE), SIP_BYE_RX + APP_CALL_PEER_END

Occupied (established or ringing) + INVITE from another peer -> 486 to the
new caller (carrying the new caller's call_id, sent *to* that caller — not
to the current dialog peer). Duplicate INVITE for the same dialog ->
idempotent 180/200 re-response. BYE for an unknown dialog -> 200(BYE) ack
to the sender, otherwise ignored. Detach mid-dialog -> best-effort
BYE/CANCEL/603 before link teardown.

## Media gating

`OutStream` (caller media) is created only when 200 OK arrives (ACK sent);
`InStream` (callee) is created only for media matching an ESTABLISHED
dialog — the M16 "first media packet implies a call" behaviour is gone.
The M16 END-flag packet is still decoded for tolerance but never sent.

## Events

New (`stack/common/include/common/events.h`, mirrored in
`lmt/src/events.ts`): `SIP_INVITE_TX/RX`, `SIP_RINGING_TX/RX`,
`SIP_CALL_ESTABLISHED {peer, kind}` (both sides),
`SIP_CALL_FAILED {peer, reason}` with
`reason ∈ busy|declined|unreachable|timeout|cancel`,
`SIP_BYE_TX/RX`.

Legacy mapping (current frontend keeps working):
`APP_CALL_START` at INVITE, `APP_CALL_INCOMING` at INVITE receipt,
`APP_CALL_END` on local hangup (BYE/CANCEL/603),
`APP_CALL_PEER_END` on BYE/CANCEL received, `APP_STREAM_STATS` unchanged.

## Commands

`call|video <imsi>` = INVITE; `answer` / `decline` for a ringing callee;
`call end` / `video end` = BYE (established) or CANCEL (ringing);
`autoanswer <ms>|off` (default 4000 ms). `msg` unchanged.

Config knobs (`UeNodeConfig`): `first_response_ms` (6000),
`ring_timeout_ms` (20000); `set_autoanswer(ms)` at runtime.

## Tests

In-process (`stack/tests/test_e2e_nodes.cpp`): full dialog with explicit
answer (updated `UeToUeMessageAndVoiceCall`), decline (603, no media),
busy via third UE (486), caller CANCEL mid-ring, ring timeout (shortened
`ring_timeout_ms`), unreachable callee, detach mid-call, plus the
blackout regression driving the SIP dialog first.
