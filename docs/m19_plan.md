# M19 — Link adaptation (CQI→MCS), UL power control, signal-quality observability, channel-sim rework

Status: implemented (P5). Three tracks, one milestone. Builds on M10
(DMRS channel estimation), M17 (QoS bearers), M18.

## Track A — channel simulator rework (`tools/channel/sim_channel.py`)

* **Parallel relaying.** The M17-era serial poll loop (one thread polling
  UL then DL with a 0.5 ms sleep, blocking `impair()` in-line) is gone.
  Now: one reader thread per ingress direction + one `EndpointWorker`
  thread per destination (BS + each UE port), connected by bounded queues
  (maxsize 2000, shed-oldest on overflow — a medium buffer, not an
  unbounded backlog). A video burst storm destined for UE2 queues only in
  UE2's worker; UE3's voice no longer waits behind it. This was the shared
  FIFO head-of-line blocking that blurred live QoS RTT differentiation.
* **Per-UE link quality**: `--ue-quality 10001=good,10002=mid,10003=poor`.
  A link is keyed by port (UL: UDP source port; DL: destination port) and
  gets its own loss rate + AWGN level. Profiles (calibrated below):
  good = 0.5 % loss / 28 dB, mid = 3 % / 21 dB, poor = 8 % / 19 dB.
  No flag = uniform legacy behaviour (global loss, no noise) — the M8 demo
  scenario and all harnesses are unaffected.
* **Absolute noise.** Per-link AWGN uses a FIXED sigma per link
  (`REF_BURST_POWER = 1/64`, the normalised-IFFT mean sample power), so
  SNR is defined at unit transmit amplitude — raising TX power genuinely
  raises received SNR, which is what makes Track C's power control real.
  The legacy `--awgn-snr` (burst-relative) and `--multipath` paths are
  unchanged. Impairments use numpy (added to `tools/requirements.txt`).
* Per-packet prints replaced by 5 s aggregate counters (log volume).
* Unit checks: `tools/channel/test_sim_channel.py` (profile parsing,
  sigma monotonicity, worker shed-oldest + relay).

## Track B — link adaptation (CQI→MCS)

**PHY** (`stack/phy/`): new generic Gray-coded square-QAM mod/demod
(`qam.h/qam.cpp`, 2/4/6 bit/symbol). Every burst is now MCS-tagged
(`frame.h`): data symbol 0 is ALWAYS QPSK and carries
`[len:16][mcs:8][payload...]` (the MCS byte is inserted after the
pack_air_bits length prefix and stripped on receive — upper layers never
see it); symbols 1..N use the burst MCS. The fixed-QPSK header keeps
foreign-unicast early-drop and mixed-rate reception working; a legacy peer
would misread a high-MCS burst (single-version simulator, documented).

**Measured decode curves** (bench: 120 B bursts, absolute-noise AWGN at
the channel sim's sigma model — this receiver's PSS timing + unprotected
air-frame headers make whole-burst survival the real constraint, not BER):

    qpsk : 18 dB 61% | 20 dB 97% | >=22 dB ~100%
    16qam: 26 dB 70% | 28 dB 97% | 30 dB 100%
    64qam: 30 dB 10% (floor beyond any simulated link)

**Honest 64QAM assessment**: implemented and round-trips cleanly
(`Frame.McsRoundTripAllRates`), but its practical floor is past 30 dB, so
the CQI ladder never selects it. 16QAM alone carries the adaptation
story, exactly the trade the task allowed.

**Control loop**: the UE EWMAs the DMRS SNR of decoded DL bursts
(`UeNode::on_air_bits_with_metrics`, fed by the process shell's
`phy_rx_frame` result), maps SNR→CQI (2 dB/index, clamp 1..15,
`ue_node.cpp:378`) and reports changes via a new MAC CE
(`LCID_CQI_REPORT=56`, change-only). The BS maps CQI→MCS per downlink
flow (`bs_node.cpp` CQI case: 16qam at cqi>=14 ~= 26 dB, else qpsk — from
the measured curve, not textbooks) and tags each DL DATA burst via the
extended radio callback `set_air_send_ex` (`bs_node.cpp:1157`);
broadcast/RACH/control stays QPSK. UL stays fixed QPSK (documented
simplification: the BS would need per-UE UL grants to signal UL MCS, and
the UL HARQ pipe, not modulation, is the UL bottleneck).

**SNR estimator fix (important)**: the M10 DMRS SNR proxy compared the
received DMRS against the unit-scale reference, so a burst arriving at
gain g≠1 (UE TX power!) read |g−1| as noise — the TPC loop was a positive
feedback runaway (power up → "SNR" down → more power up, clamp at +13
dB). The estimate is now gain-invariant: scalar ĝ from the DMRS power
ratio first, noise = |rx − ĝ·ref|² (`frame.cpp:233`). Bench: reported
SNR tracks link+gain exactly (28 dB link + 13 dB TX → 41.1 dB).

## Track C — UL power control + signal quality

* **Open loop** (UE, `service_link_metrics`, 1 s): synthetic power
  budget — BS reference 30 dBm, UL arrival target −50 dBm —
  `tx = clamp(−50 + pathloss + tpc_accum, −30, +23)` with
  `pathloss = 30 − rsrp`. The shell scales the whole UL burst (preamble
  included) by `ue.tx_gain()`.
* **Closed loop** (BS, `sweep_tpc`, `bs_node.cpp:425`): per-flow UL SNR
  EWMA vs a 23 dB target (measured QPSK floor ~20 dB + 3 dB margin),
  ±1 dB TPC MAC CE (`LCID_TPC=55`) at most every 500 ms; UE folds it into
  `tpc_accum_db_` (±15 dB clamp).
* **Telemetry**: `LINK_QUALITY {c_rnti, rsrp, sinr}` ~1/s per registered
  UE (LMT signal bars), `TX_POWER_CHANGE {c_rnti, dbm}` on ≥0.5 dB deltas
  ≥1 s apart, `CQI_REPORT {c_rnti, cqi, snr_db}` change-only,
  `MCS_CHANGE {c_rnti, mcs, direction}` on every re-selection. Live, all
  three UEs' UL arrival SNRs converge to 21–23 dB with TX powers of
  −4 / +3 / +5 dBm (good/mid/poor).

## Live evidence (3 UEs, `--ue-quality 10001=good,10002=mid,10003=poor`)

* Per-UE differentiation: SINR 28/21/19 dB → CQI 15/11/10 → ue1 16QAM,
  ue2/ue3 QPSK; all three attach (poor link succeeds on RACH retries).
* QoS differentiation restored after the channel-sim rework
  (ue1 video→ue2 + voice→ue3 concurrently):
  voice ALONE RTT **66 ms** loss 0 → voice under video flood **~205 ms
  marginal, loss 0** vs video **320 ms** on the same flood. M17-era
  equivalents were 315/350 (10 % apart, channel HOL dominating): the gap
  is ~36 % now and absolute RTTs fell ~110 ms.
* PHY CPU at media rates (300 B bursts, this machine): 3.6 ms (QPSK) →
  3.35 ms (16QAM) → 3.24 ms (64QAM) per demodulated burst — higher MCS
  means fewer OFDM symbols, so link adaptation slightly REDUCES CPU.

## Events

New (events.h + `lmt/src/events.ts` mirror): `CQI_REPORT {c_rnti, cqi,
snr_db}`, `MCS_CHANGE {c_rnti, mcs, direction}`,
`TX_POWER_CHANGE {c_rnti, dbm}`, `LINK_QUALITY {c_rnti, rsrp, sinr}`.
New MAC CEs: `LCID_CQI_REPORT (56)` UL, `LCID_TPC (55)` DL.

## Commands / flags

`start_demo.sh --ue-quality 10001=good,10002=mid,10003=poor` (passed
through to the channel simulator). `./start_demo.sh` and `--with-demo`
default to the legacy uniform channel.

## Tests

New: `Frame.McsRoundTripAllRates` (QPSK/16QAM/64QAM burst round-trips,
burst length shrinks with rate), `E2eNodes.LinkAdaptationFollowsChannelQuality`
(injected metrics: 28 dB→16QAM, 21 dB→QPSK, 27 dB→16QAM),
`E2eNodes.TpcSteersUeTxPowerTowardsTargetSnr` (weak UL → power climbs,
strong UL → power falls), `tools/channel/test_sim_channel.py`. Updated
with comments: `Frame.CleanRoundTripPreservesBitsAndPci` (MCS-tagged
bursts zero-pad the tail — prefix + zero-tail compare). Full suite
167/167 green, event catalog 129 entries in sync.

## Simplifications / deviations

* UL MCS fixed QPSK; 64QAM implemented but never selected (measured floor
  too high for simulated links).
* RSRP is the received burst power in dB (synthetic units); per-link
  pathloss attenuation is NOT modelled — links differ in noise and loss,
  so RSRP is roughly uniform and SINR carries the differentiation.
* No MCS hysteresis (EWMA smoothing damps flap; CQI change-only reporting
  bounds the event rate).
* Bench decode curves were measured with 120 B bursts; shorter control
  bursts survive slightly better, which is why the 19 dB "poor" link
  still attaches (with retries).
