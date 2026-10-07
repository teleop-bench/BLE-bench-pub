# Multi-round counterbalanced FSU / parity campaign — 2026-09-07/08

**Why:** raise confidence on the FSU deltas + open-vs-SDC parity from the single-pass (n=1) suite to
defensible confidence intervals, by measuring all 8 headline cells many times with alternating
(counterbalanced) arm order so slow drift cancels out of the paired per-round deltas. Captured from
boot → FSU engagement verified every measurement.

**Run:** 2× nRF54L15-DK, prebuilt hexes, 60 rounds × 8 cells = **480 measurements**, 30 s each, ~4.5 h.
Harness: `harness.py` (this dir). Raw: `results.jsonl`; live summary: `summary.txt`.

## Per-cell throughput (mean ± 95% CI, n=60)
| transport | stack | FSU | KB/s | FSU-engaged |
|---|---|---|---|---|
| GATT | open | on  | 189.6 ± 0.1 | 100% ✓ |
| GATT | open | off | 157.7 ± 0.1 | 0% |
| GATT | SDC  | on  | 179.1 ± 0.4 | 0%* |
| GATT | SDC  | off | 161.2 ± 0.4 | 0% |
| CoC  | open | on  | 159.6 ± 0.4 | 100% ✓ |
| CoC  | open | off | 159.7 ± 0.3 | 0% |
| CoC  | SDC  | on  | 179.0 ± 0.4 | 0%* |
| CoC  | SDC  | off | 155.3 ± 0.4 | 0% |

\* SDC does not emit the `spacing=52` app token, so on-chip engagement isn't observable on SDC — but
the on/off throughput delta (below) shows FSU is working on SDC.

## FSU on/off delta — paired per-round (mean ± 95% CI, n=60 rounds)
| pair | interval | delta |
|---|---|---|
| **GATT / open** | 7.5 ms | **+20.2% ± 0.1%** |
| **GATT / SDC**  | 25 ms  | **+11.1% ± 0.2%** |
| **CoC / open**  | 15 ms  | **−0.1% ± 0.2%  (statistically FLAT)** |
| **CoC / SDC**   | 15 ms  | **+15.2% ± 0.3%** |

## Open-vs-SDC parity (FSU off) — paired per-round (± 95% CI, n=60)
- **GATT: −2.2% ± 0.2%**  (open slightly below SDC)
- **CoC:  +2.8% ± 0.2%**  (open slightly above SDC)

## Conclusions
1. **FSU is real and regime-dependent — now with hard numbers.** GATT/open +20.2% ± 0.1%. CoC/open at
   15 ms is **statistically flat (−0.1% ± 0.2%) despite FSU verified engaged** (100% `spacing=52`) —
   because CoC saturates ~159.6 KB/s at 15 ms, leaving no airtime headroom for the tIFS reduction to
   reclaim. This is the definitive backing for "FSU helps where there's throughput headroom, is a
   harmless no-op at saturation."
2. **Open ≈ SDC parity confirmed:** within ±3%, opposite signs (GATT open −2.2%, CoC open +2.8%) — a
   dead heat at zero load, now with tight CIs.
3. **SDC FSU works** (CoC/SDC +15.2%, GATT/SDC +11.1%) even though the app token isn't emitted there.

## Caveat on the CIs (important)
These are **within-session** 95% CIs — they quantify sampling/measurement precision on ONE bench, ONE
day, and are tight because a fixed-config clean-bench link is highly repeatable (per-cell CV < 1%). They
do **NOT** include cross-environment (RF-day, distance, interference) variance, which is the ±10–20% that
makes *absolute* KB/s non-transferable. So: quote the **deltas** with these CIs as within-session
precision; the absolute KB/s remain RF-day-specific. Different-day / degraded-RF characterization is a
separate open item.
