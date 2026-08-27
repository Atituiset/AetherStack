# M21 — 5G-AKA-style subscriber authentication

Status: implemented (P7). Replaces the M12 simplified HMAC challenge/
response with a faithful AKA structure (TS 33.501 shape) while keeping
HMAC-SHA256 as the f1-f5 analog (documented simplification).

## Algorithm core (`stack/nas/…/aka.h`, `aka.cpp`)

* `f1..f5` (+`f1*`/`f5*`) = HMAC-SHA256(K, label || fields) with a one-byte
  domain label standing in for MILENAGE/TUAK. Sizes follow real AKA:
  RAND 16 B, AUTN 16 B (SQN^AK 6 || AMF 2 || MAC 8), RES/XRES 16 B,
  AUTS 14 B (SQNms^AK* 6 || MAC-S 8), SQN 48-bit, AMF 0x8000.
* `generate(K, sqn, rand)` builds the full vector: SQN^AK, MAC (f1 over
  SQN||RAND||AMF), XRES (f2), CK (f3), IK (f4), and a KASME analog =
  `HMAC(CK||IK, "kasme" || SQN^AK)` (`aka.cpp:kasme`) — this is the session
  key wired into the existing PDCP ChaCha20/HMAC context (plumbing
  unchanged, key source swapped from the M12 ad-hoc KDF).
* UE helpers: `verify_autn` (recover SQN, recompute XMAC),
  `build_auts`/`verify_auts` (synchronisation failure round trip).

## Message flow

    UE                                              BS (UDM/HSS)
    | ---- ATTACH_REQUEST {imsi} ------------------> |
    |        per-IMSI SQN++ , fresh RAND,            |
    |        AV = (SQN^AK, MAC, XRES, CK, IK, KASME) |
    | <--- AUTH_REQUEST [RAND:16 || AUTN:16] ------- |  NAS_AUTH_VECTOR
    | verify_autn: XMAC==MAC?  (no -> AUTH_FAILURE   |  {imsi, rand, sqn_masked}
    |   cause=mac, reject locally)                   |
    | SQN > SQNms?  (no -> AUTH_FAILURE cause=synch  |
    |   with AUTS = SQNms^AK* || MAC-S)              |
    | ---- AUTH_RESPONSE [RES:16] -----------------> |  NAS_AUTH_RES {imsi, res}
    |                                                | RES==XRES -> registered,
    |                                                |   session key = KASME
    |                                                |  NAS_AUTH_SUCCESS {imsi}
    | <--- ATTACH_ACCEPT {tmsi} -------------------- |
    | RES mismatch -> NAS_AUTH_FAIL {cause=res_mismatch} + ATTACH_REJECT

* **MAC failure** (UE side, `nas_ue.cpp:on_message`): the network could
  not prove knowledge of K — `AUTH_FAILURE cause=0x14`, no RES is ever
  sent; the BS drops the pending challenge (`NAS_AUTH_FAIL {imsi, mac}`).
* **SQN freshness** (UE side): accepted SQN must exceed the highest
  previously accepted one (48-bit counter per USIM). On stale SQN the UE
  answers `AUTH_FAILURE cause=0x15` with AUTS; the BS verifies MAC-S
  against the failed vector's (K, RAND), adopts SQNms as the new counter
  and retries with a FRESH RAND and SQN = SQNms+1 (`nas_bs.cpp`,
  AUTH_FAILURE case; mirrored in the AMF).
* **AUTH_FAILURE wire format** carries the IMSI
  ([cause:1][imsi_len:1][imsi][AUTS]) — a simulator shortcut so the
  network can attribute failures without a session lookup (documented
  deviation from 3GPP).
* Both cores upgraded: embedded `NasBs` (`stack/nas/src/nas_bs.cpp`) and
  the M15 split-core `Amf` (`stack/cn/src/amf.cpp`) share `nas/aka.h`.
* Unknown IMSIs (tests without USIM provisioning) keep the legacy
  open-access attach path, unchanged.

## Key freshness

Every challenge uses a fresh RAND (std::random_device-seeded mt19937_64
by default; `NasBs::set_rand_fn` pins it for tests) and the per-subscriber
SQN advances per challenge — so every attach yields a different KASME
(`AkaHappyPathFreshKeysPerAttach` asserts K1 != K2 across attaches and
`ue.session_key() == bs.session_key(tmsi)`).

## Events (final names — frontend contract)

* `NAS_AUTH_VECTOR {imsi, rand, sqn_masked}` — BS/AMF on AUTH_REQUEST TX.
  rand = first 8 hex chars of RAND; sqn_masked = full 12-hex SQN^AK.
* `NAS_AUTH_RES {imsi, res}` — UE on AUTH_RESPONSE TX (first 8 hex of RES).
* `NAS_AUTH_SUCCESS {imsi}` — BS/AMF on RES==XRES.
* `NAS_AUTH_FAIL {imsi, cause}` — cause ∈ `mac` | `synch` | `res_mismatch`.
* Existing `NAS_ATTACH_*` / `NAS_STATE_CHANGE` / `SEC_ENABLED` events are
  unchanged. **Removed** (flagged for the frontend): the M12-era
  `NAS_AUTH_CHALLENGE_TX`, `NAS_AUTH_RESPONSE_TX` and `NAS_AUTH_OK` —
  superseded by the three events above; `NAS_AUTH_FAIL` kept its name but
  its fields changed from `{tmsi}` to `{imsi, cause}`.
  `NAS_AUTH_RESP_UNKNOWN` / `NAS_AUTH_REQ_IGNORED` retained.

## Demo provisioning

`start_demo.sh` provisions all three UEs: `bs --subscriber imsi:hexkey`
(repeatable, new flag) + `ue --usim-key hexkey` (new flag). Demo-only
ASCII/hex keys, not secrets. `--with-demo` scenario passes with AKA in
the loop.

## Tests

New (`stack/tests/test_e2e_nodes.cpp`, direct NasUe↔NasBs fixture with a
pinned RAND source): `AkaHappyPathFreshKeysPerAttach` (happy path, key
agreement, K1!=K2, SQN advances), `AkaMacFailureRejectsNetwork`,
`AkaStaleSqnTriggersAutsResync` (AUTS verified, counter resynchronised,
retry succeeds), `AkaResMismatchRejects` (tampered RES -> ATTACH_REJECT),
`AkaDerivedKeyDrivesPdcpBothWays` (protect/unprotect both directions with
the derived KASME, foreign key fails). Comment updates in
`WrongUsimKeyIsRejected` (now fails on the UE's AUTN MAC check) and
`AuthenticatedAttachWithEncryptedUserPlane`. Full suite 177/177 green,
event catalog 133 entries in sync.

## Simplifications

* HMAC-SHA256 with label bytes as f1-f5 (no MILENAGE/TUAK AES op-chain);
  CK/IK/KASME are 32 B (HMAC length) instead of 16 B.
* AUTH_FAILURE carries the IMSI (not in 3GPP) for deterministic
  attribution on a shared simulator bus.
* SQN freshness is "strictly greater than last accepted" (no sliding
  window array) — sufficient with a single attachment point.
* RES mismatch with multiple concurrent outstanding challenges falls back
  to a generic `NAS_AUTH_RESP_UNKNOWN` (single-challenge case names the
  IMSI; in practice attaches serialise).
