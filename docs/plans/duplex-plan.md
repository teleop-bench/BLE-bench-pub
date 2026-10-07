# Plan: symmetric bidirectional (duplex) saturation on nRF54 — systematized

> **Historical planning record.** Kept as written for provenance; its status notes and numbers may
> be superseded or retracted. For current results and their status, see
> [EVIDENCE-INDEX](../EVIDENCE-INDEX.md) and [LESSONS](../LESSONS.md).
> In particular, the **FSU duplex +14.24%** and the **≤0.1% imbalance** below are withdrawn: the
> +14% did not reproduce head-to-head, and the even split is an echo-rig coupling artifact.

*Status: EXECUTED 2026-08-06 — 22/22 runs complete, analyzed strictly under
the registered contract (`analyze_duplex.py` rev 2 + `duplex.csv`): symmetric
~180 KB/s aggregate on both controllers at 7.5 ms, imbalance ≤0.1% all runs;
FSU duplex +14.24% [12.36, 16.12] n=6 pairs at 50 ms (~200 KB/s) —
EXCEEDS the registered +7.2% airtime-model prediction below (model floor
199 KB/s vs FSU-off measured ~175.5; mechanism unresolved). All
confirmations green; zero flags; runner printed DUPLEX-COMPLETE (marker is
runner-side, recorded in provenance).*

*Status: plan (2026-08-06). Origin: the FSU campaign's echo configuration
accidentally measured the symmetric point (~94+94 KB/s, SDC @7.5 ms). This
plan turns that into formal cells across arms. Literature context: the
directions trade off against a near-constant aggregate; the symmetric point
is the 50/50 split.*

## Instrument

The echo configuration, embraced: central blasts 244 B writes to the ping
characteristic; the peripheral echoes each as a full-size notification
(sink mode OFF). Traffic is coupled 1:1 by design — but symmetry is a
MEASURED claim, not assumed (echo attempts can silently fail; see the
analysis contract below).
- Forward rate: peripheral's `rxkBps` (existing counter).
- Reverse rate: central's echo-notification bytes/s (`exkBps` — new counter
  in the notify path).
- Aggregate = sum; symmetric-split validity check: forward ≈ reverse.
Caveat (pre-registered): echo couples the directions — this measures the
50/50 point only, not the full trade-off curve (independent bidirectional
sources are future work); reverse traffic is ATT notifications, forward is
writes (both full-size on air).

## Airtime-model predictions (2M, DLE-251, both packets full)

pair = 1048 + IFS + 1048 + IFS:
- 150 µs spacing: 2396 µs → **~99 KB/s each way (~199 aggregate)**
- 70 µs (FSU): 2236 µs → **~106 KB/s each way (+7.2%)**
Accidental datum: 94–95 each way at SDC 7.5 ms events (≈95% of model —
event-boundary overheads).

## Cells (standard protocol: 5 runs × ≥3 min, reset-separated; the FSU
comparison as counterbalanced pairs like U0/U1)

| Cell | Arm | Interval/events | Spacing | Prediction |
|---|---|---|---|---|
| D-open75 | open Zephyr | 7.5 ms | 150 µs | open event-fill vs SDC comparison |
| D-sdc75 | SDC | 7.5 ms events | 150 µs | formalizes the accidental 94+94 |
| D-sdc50 | SDC | SCI 50 ms + 50 ms events | 150 µs | fewer boundaries → closer to 99+99 |
| D-fsu50 | SDC | same | **70 µs** | **~106+106 — FSU's duplex dividend** |

D-sdc50 vs D-fsu50 run as counterbalanced pairs (AB BA BA AB AB BA);
FSU HCI-verified per run; on-air capture caveat inherited from the FSU work.

## Deliverables

- Table + per-direction/aggregate numbers with model comparison per cell.
- Executive summaries: replace the "incidental" symmetric datum with formal
  numbers across arms.
- Evidence under `debug-evidence/` (logs now tracked), analysis script + CSV
  per the U-cell convention.

## ANALYSIS CONTRACT — registered 2026-08-06 ~21:50 local, BEFORE reading any
## dx-* results (campaign in flight; firmware/procedure frozen for this series)

- **Estimators (per run):** forward = peripheral `rxkBps` per-second values
  >10, drop first 15 kept samples; reverse = central `exkBps` (BLASTC lines),
  same convention; run value = mean of remaining samples; aggregate =
  forward-mean + reverse-mean. kB = 1024 bytes throughout.
- **Warm-up:** the runner's 45 s pre-capture + the 15-sample settle drop.
- **Symmetry is a MEASURED claim, not "by construction"** (the peripheral
  discards `bt_gatt_notify`'s return code — echo attempts can silently
  fail): report |fwd−rev|/mean per run; >5% flags the run ASYMMETRIC.
  Flagged runs are REPORTED AND INCLUDED — no retroactive exclusion this
  series; a clean rerun adds notification-error counters.
- **FSU pair analysis (D-sdc50 vs D-fsu50):** per-pair percentage delta on
  the aggregate (and per-direction, reported), mean + 95% paired t-CI
  (n=6, t=2.571) — within-session, one board pair, per the U convention.
- **Per-run confirmations required:** PHY 2/2; MTU 247; 50 ms cells:
  `rate_changed interval=50000`; FSU arm: `spacing=70`. A FSU-arm run
  without spacing confirmation is EXCLUDED (U-cell precedent — the only
  preregistered exclusion); any other missing confirmation flags the run.
- **Incomplete/disconnected runs:** <100 valid seconds or a post-settle
  disconnect → included with annotation; zero valid window → missing, CI
  over remaining pairs with n stated.
