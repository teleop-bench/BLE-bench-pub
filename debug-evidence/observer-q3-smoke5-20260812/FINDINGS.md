# Q3 f100 smoke-5 — FIRST METRICS-OK (pre-accept; QUARANTINED)

**2026-08-12 · fsu-m0 HEAD 9999e040 · analyzer METRICS-OK · cell QUARANTINED
(`q3-preaccept-smoke`)**

The registered Q3 analyzer reached **METRICS-OK** end-to-end on real hardware with the
rev-8 dedicated-CC3 capture. The cell is **held at `q3-preaccept-smoke` by design** —
promotion is a SEPARATE reviewed commit, NOT made here.

## Every gate passed
| gate | result |
|---|---|
| FSU config assert (both endpoints, pre-flash) | PASS (tIFS=52, 1M-only, echo off) |
| Q2 structural + config binding | PASS |
| whole-cell retention | central **97.5 %** / periph 98.1 % (≥95 %) |
| peripheral `Q3FSU-DONE` `initiator=PEER` | PASS |
| phase-specific retention (F-time) | 0.95 / 0.981 / 0.963 / 0.981 (all ≥0.95) |
| plateau shape (real-cell-calibrated) | PRE {iqr 8, drift 0, conc 1.0} / POST {iqr 8, drift 2, conc 1.0} |
| **RF PRIMARY step** | **802 t (\|d\|=2 ≤ 4) — AGREEMENT** |
| on-chip cross-val | on-air 50.12 µs vs on-chip 150−100 = 50 µs, \|d\|=0.12 µs (≤8) — **OK** |
| change point (diagnostic) | inside window |
| manifest provenance | all sha256 real (incl. endpoint ELFs); q3_contract + assert bound |

## rev-8 dedicated-CC3 — validated, and it CONFIRMS the CC0 hypothesis
```
TIFSBIN tifs=150 n=383 min=140 med=140 max=140 drop=0
TIFSBIN tifs=100 n=432 min= 90 med= 90 max= 90 drop=0   -> on-chip delta 50 µs
TIFSDIAG prog=818 arm=815 cons_tx=2 cons_done=813 stale_done=3 dup=0
         consume_cc3=815 cleanup_error=0 cleanup_stale=0 cc0_changed=0 drop=0
```
- **`arm=815`** (vs rev-7's 577/593) and **central retention 97.5 %** (vs rev-7's ~70 %):
  moving the capture off CC0 to CC3 **eliminated the peripheral-RX perturbation**. This
  upgrades the smoke-4b CC0-corruption HYPOTHESIS to CONFIRMED.
- `cleanup_error=0, cleanup_stale=0, cc0_changed=0, drop=0`: no leaked redirects, CC0
  never moved while redirected, no dropped samples — the arm/consume/restore lifecycle
  is clean across all 815 samples.

## Record corrections resolved
- smoke-4b's "CC0 corruption is the cause" (then a hypothesis) is now **confirmed**.
- smoke-5 is the **first end-to-end FULL-analyzer gated pass** (METRICS-OK). smoke-3 was
  an RF-primary-only gated pass; smoke-5 clears every leg including on-chip cross-val.

## What this establishes (and its scope)
The open Zephyr M0 FSU responder physically reduces its achieved on-air tIFS by ~50 µs
at the FSU transition (150.5 → 100.5 µs), independently corroborated by the on-chip
turnaround histogram (140 → 90 µs), with the peer's participation, retention, and
plateau stability all gated. This is a **strong, fully-gated pre-accept result for the
f100 arm at 1 M**, NOT yet an accepted cell.

## NOT done here (awaiting review)
- No promotion. The f100 accepted-cell path stays quarantined; promotion from
  `q3-preaccept-smoke` to an accepting verdict is a SEPARATE reviewed commit.
- f150 steady-state control arm + ABBA are still pending before an accepted collection.
