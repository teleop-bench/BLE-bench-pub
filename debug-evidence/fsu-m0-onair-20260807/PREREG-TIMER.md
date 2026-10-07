# §6.1 physical-timing qualification — TIMER-capture preregistration (FROZEN)

*Recorded 2026-08-08 BEFORE implementation and BEFORE any accepted data.
SUPERSEDES the FEM preregistration (`PREREG-FEM-SUPERSEDED.md`, retained
historically — that instrument was unusable here). Qualifies STEADY-STATE
frame-spacing APPLICATION only — NOT full FSU procedure correctness, NOT
spec conformance.*

## Claim boundary
- **Instrument = the controller's SHARED EVENT_TIMER, read via
  `radio_tmr_ready_get()` (CC0).** This is NOT a dedicated spare timer; it
  reuses the controller's own hardware-event capture. Result is a
  **controller hardware-event timing PROXY** — NOT on-air. Nordic defines
  PHYEND at the last transmitted/received RF bit and READY at radio
  readiness; the proxy stops at READY, BEFORE the subsequent RF TX edge, so
  it does not observe the on-air first bit. The literal on-air claim still
  requires a qualified sniffer (out of scope here).
- Supported claim if PASS: "the open-Zephyr M0 controller changes its
  RX-PHYEND→TX-READY turnaround by the negotiated spacing amount
  (150→{100,52} µs) at steady state."

## Timer arithmetic (verified in radio.c; single-timer mode CONFIRMED on build)
- `CONFIG_BT_CTLR_SW_SWITCH_SINGLE_TIMER=y`. On PHYEND the EVENT_TIMER is
  CLEARED (and captures CC2 = its own timestamp on the OLD base). The
  following READY captures **CC0 on the freshly-cleared base**, so
  **CC0 IS ALREADY the RX-PHYEND→TX-READY interval**.
- **Read `radio_tmr_ready_get()` (CC0) near the START of
  `lll_conn_isr_tx`** — there it holds the preceding RX-PHYEND→TX-READY
  interval. **NEVER compute CC0 − CC2** (mixes two timebases → meaningless).
  CC2 retained only as a packet-duration SANITY diagnostic, never
  subtracted.
- Timer resolution ~1 µs → the expected 50 µs (150→100) and 98 µs
  (150→52) changes are easily distinguishable.

## Implementation constraints (review points)
1. **No printing in the radio ISR.** Write fixed-size records to a static
   RING BUFFER in the ISR; emit later from thread/ULL context.
2. **Freshness/validity gating.** Set a valid-RX flag in `isr_rx` ONLY on a
   fresh CRC-valid data reception; consume it in the following `isr_tx`.
   Reject setup transitions, empty events, failed CRC, stale CC0.
3. **Per-record metadata (to audit pairing):** CC0 interval, configured
   `tifs_tx_us`, PHY, role/direction, connection + event sequence,
   validity state.
4. **Marker-disabled control:** a build with the capture compiled out must
   show unchanged link health AND unchanged completion-gap behavior — the
   readout must not perturb what it measures.

## Capture design (FROZEN)
- Geometries: (A) 1M 150→100 µs; (B) 2M 150→52 µs.
- Per arm/geometry: ≥5 accepted (fresh+valid) RX→TX intervals from ≥2
  independent connections; discard the first 2 s after `FSU: updated`
  (adoption transient). 150 baseline captured pre-request / on the [150..150]
  control build.
- **PRIMARY = arm-to-arm DELTA** (baseline − reduced). READY precedes the
  transmitted first bit by a FIXED radio-chain time, so the absolute value
  need NOT equal 150/100/52 µs; only the CHANGE must track the negotiated
  reduction. Absolute value reported as calibration only.
- Estimator: median CC0 interval per arm over accepted records.

## Accept / reject (FROZEN)
- Reject a record without the fresh+valid flag, or during the 2 s
  post-adoption window, or with a disconnect/timeout in the run.
- Reject the reduced arm's run without `FSU: updated status=0x00
  spacing=<target>`. Reject the geometry if the 150 baseline is unstable.
- Rejected data quarantined (hash + reason); explicit accepted-record
  manifest gates analysis.

## PASS / outcomes (symmetric)
- PASS: median-CC0 DELTA matches the negotiated reduction within timer
  resolution (±~1 µs) + run-to-run spread: 150→100 ≈ 50 µs; 150→52 ≈ 98 µs.
- Delta absent/partial: controller does NOT change the turnaround despite
  HCI confirmation — a real reportable negative.
- Delta off-model: investigate PHY/pairing/offset before interpreting.
- PASS supports ONLY the steady-state-turnaround claim; does NOT upgrade
  any throughput/latency/duplex number; lets the existing 1M +5.72% be
  reported physically-corroborated (with that result's registered caveats).

## Note on the GPIOTE route
The claim that GPIOTE markers are "blocked by the same root cause as FEM"
is an UNVALIDATED, higher-risk cross-domain (DPPIC10↔GPIOTE20 via PPIB)
inference — NOT established. It is simply unnecessary given this in-domain
timer capture.


## POSITIVE-CONTROL AMENDMENT (frozen 2026-08-08, rev-2 instrument, before data)
Before the FSU arms can be interpreted, the instrument must be shown to
RESOLVE a known physical tifs change (else a flat FSU result is
uninterpretable). Control:
- STOCK non-spec low-latency path: 2M, 1 ms interval, configured tifs 150
  vs 52 us (ull_conn_update_parameters applies 52 at the low-lat operating
  point — a PREVIOUSLY DEMONSTRATED stock scheduler reduction, NOT a
  spec-standard path and NOT independent on-air ground truth).
- >=2 independent connections per arm; drain the rev-2 histograms; PRIMARY
  = arm-to-arm DELTA of the CC0 median (from the histogram).
- PASS (instrument valid): CC0 median moves by ~98 us (150->52), tracking
  the known change within timer resolution + spread.
- FLAT: instrument cannot resolve tifs (blind) -> RETIRE the proxy; fall
  back to sniffer or defer.
Only after a PASSing positive control do we RE-RUN the 2M/7.5 ms FSU arm
with rev-2 pairing and >=5 accepted post-settle records / >=2 connections.
A repeated flat FSU result THEN (control moved, FSU didn't) would support
an FSU-application gap — not before. Also run the capture-disabled
perturbation control.


## REV-2/3 AMENDMENT (dated 2026-08-08, supersedes the ring-buffer clauses above)
The frozen text above specified a static RING of fixed-size records with
per-record connection/event metadata. That implementation OVERFLOWED
silently (256-ring, ~130/s production vs ~13/s drain; seq jumped 2->1539)
and had a PAIRING bug. Those implementation clauses are SUPERSEDED by the
rev-3 instrument:
- EXACT 1 us CC0 histogram per (tifs, phy) bin -> preregistered MEDIAN is
  exact at ~1 us resolution (the earlier coarse/min-max form is withdrawn).
- BOUNDED aggregation with EXPLICIT LOSS ACCOUNTING: a record with no free
  bin or CC0>255 increments `drop`; ACCEPTANCE REQUIRES drop=0.
- Correct pairing: CC0 attached to the tifs that PRODUCED it (tifs_prog_prev).
- The FIRST transition after each (re)connection is SKIPPED (stale latch).
- Post-settle isolation via an ATOMIC bt_ctlr_tifs_clear() called once
  after the settle (app CONFIG_APP_TIFS_CLEAR_S); bins thereafter are
  cumulative post-settle. Drain SNAPSHOTS under irq_lock(), formats after
  unlock.
REV-4 additions (2026-08-08): freeze-then-snapshot (the KB-scale memset
runs OUTSIDE irq_lock; the lock holds only tiny flag flips; the drain
reads FROZEN bins with no lock/copy — safe on the 1 ms path). The
"skip first transition" is the FIRST POST-CLEAR transition only (primed
by the clear, not a per-reconnect hook); runs are reset-isolated to one
connection and a mid-run reconnect is REJECTED (disc must be 0). The
reported median is the LOWER median (first value whose cumulative count
reaches ceil(n_valid/2)); the even/odd ~1 us convention is immaterial
against the ~98 us target delta — preregistered as lower-median.

HISTOGRAM-SNAPSHOT ACCEPTANCE (replaces per-record-manifest): a run's
accepted datum per (tifs,phy) is the post-clear snapshot's {n_valid, min,
median, max, drop}; ACCEPT only if drop=0 and n_valid>=5. >=2 independent
connections/arm = separate reset-isolated captures, each its own snapshot.
Also required: the capture-DISABLED perturbation control (link health +
completion-gap unchanged vs BT_CTLR_TIFS_CAPTURE_BENCH=n).


## ACCEPTED-DATA PROTOCOL (frozen 2026-08-08, before any accepted run)

### Positive control — cross-connection estimator (FROZEN)
- Run order: A150, B52, B52, A150 (A = configured 150 us; B = the stock
  non-spec low-latency reduction to 52 us). Each cell is a SEPARATE,
  reset-isolated connection.
- Per cell: ONE lower-median CC0 from the post-clear frozen snapshot.
- Form TWO paired A-B deltas (pair1 = A150[0]-B52[0], pair2 = A150[1]-B52[1]
  by run order) and REPORT BOTH deltas + their range. With only two pairs,
  do NOT manufacture a CI.
- Per-cell validity (all required): drop=0; n_valid>=5; PHY as intended;
  disc=0 (no mid-run reconnect); TIFS cleared logged after settle.
- PASS (instrument VALIDATED, only): BOTH deltas approximately 98 us
  (150->52) within the declared tolerance = timer resolution (+/-1 us) plus
  the observed within-cell spread (min..max of the contributing bins).
- FLAT / off-target: "PROXY VALIDATION FAILED; cause undetermined; RETIRE
  the proxy" (NOT necessarily "instrument blind" — the stock reduction is
  not independent on-air ground truth, so a null cannot localise the
  cause). Fall back to sniffer or defer.

### Capture-disabled perturbation control (numeric, FROZEN)
Compare a BT_CTLR_TIFS_CAPTURE_BENCH=y build vs an =n build at the same
operating point. UNCHANGED is defined numerically:
- completion-gap median within +/-2 us and its min within +/-2 us;
- RTT-mean median within +/-1% ; disc delta = 0 ; pong-timeout delta = 0 ;
  cancels delta = 0 over the compared window.
Any breach => the readout perturbs what it measures => fix or retire.

### FSU rerun (only after a PASSing positive control) — timing proof
The 2M/7.5 ms FSU arm rerun must PROVE the post-settle clear landed after
adoption: the logs must show the central 'FSU: updated spacing=52' HCI
timestamp AND the periph 'TIFS cleared at t=..' timestamp, with
clear_time - fsu_update_time >= 2 s. Do NOT rely on the default 18 s clear
assumption — verify from the logs per run. Then the same A/B/B/A order,
>=5 accepted post-settle records/cell, >=2 connections/arm; a repeated
FLAT FSU result WHILE the control moved supports an FSU-application gap.
