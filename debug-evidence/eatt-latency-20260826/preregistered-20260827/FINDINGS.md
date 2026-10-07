# EATT latency-under-load — PUBLICATION-GRADE run (preregistered gate PASS), 2026-08-27

**Canonical EATT result.** Supersedes the 2026-08-26 first run (invalid: seq-correlation defect) and
the 2026-08-27 matching-pong rerun (valid *by source semantics via a documented post-hoc gate
correction*). This run fixes both: the corrected gate was **declared before the run and checked
automatically**, and it **passed**.

## Method
- **Firmware:** `eattlat-central`/`eattlat-periph`, matching-seq waiter (`notify_cb` wakes only on the
  outstanding sequence) + per-probe `k_sem_reset`; the stale-reply counter is renamed **`late_pong`**.
- **Design:** GATT ping-pong stop-signal (8 B write→notify) vs a bulk write-load ramping 0→150 KB/s
  (7×60 s), @15 ms, 2M. EATT-ON = 4 ECRED bearers (`eatt=4`); EATT-OFF = same encrypted link, one
  bearer. **ABBA** (ON/OFF/OFF/ON), each arm reset-isolated.
- **Preregistered validity gate (all four arms):** (a) no `late_pong` before the first timeout;
  (b) cumulative `late_pong ≤ cumulative timeouts`; (c) recorded RTTs are exact-seq matches (guaranteed
  by the callback). **Result: PASS** — on1/off1/off2/on2 late_pong 124/50/72/98 all ≤ timeouts
  159/71/99/125; no late_pong before any first timeout.
- **Reporting:** **timeouts counted as budget failures** (in the denominator); **p99 omitted** (the
  200 ms ping timeout censors that region so a "p99≈200" is a censoring artifact); completed-sample
  median reported.

## Result — timeout-inclusive (pooled ON=on1+on2 vs OFF=off1+off2)
| load KB/s | ON >30 ms-or-to | OFF | ON >100 ms-or-to | OFF | ON median | OFF median | ON to% | OFF to% |
|-----------|------|-----|------|-----|-----------|------------|--------|---------|
| 25   | 7.3%  | 4.8%  | 1.5%  | 2.7%  | 26  | 26  | 0.0% | 0.0% |
| 50   | 11.3% | 10.2% | 6.6%  | 6.4%  | 26  | 26  | 0.5% | 0.4% |
| 75   | 18.2% | 15.7% | 9.5%  | 9.7%  | 26  | 26  | 1.1% | 0.4% |
| 100  | 28.3% | 25.4% | 17.6% | 18.9% | 26  | 26  | 2.6% | 0.7% |
| 125  | 45.5% | 40.2% | 32.5% | 35.2% | 26  | 26  | 6.2% | 3.6% |
| 150* | 86.3% | 85.1% | 66.3% | 77.2% | **146†** | **193** | 12.8% | 10.6% |

*saturation. †**median of the two run medians** (ON replicates 141/152 ms; OFF 193/193 ms) — *not* a
pooled sample median. The saturated `>30 ms-or-to` and timeout columns are pooled but **not directionally
stable across replicates** (OFF: 80.3/90.6 %; timeout 5.1/16.9 %) — see Conclusion.

## Gate (per-line verified)
`reanalyze.py` re-checks the gate **on every telemetry line** (not just the final counters — 3rd-review
point 1): all four arms have **0** lines with `late_pong > tot_to` and **0** `late_pong` before the
first timeout → **PASS throughout** (`reanalysis.txt`).

## Recommended headline
> At saturation, EATT **reproducibly reduced the severe `>100 ms-or-timeout` tail** and shifted the
> completed-sample median below the single-bearer ~193 ms mode. It **did not** make the stop signal meet
> a 30 ms deadline — **both arms missed it on >85 % of probes.** Lower-load benefits were negligible, and
> the saturated effects on the 30 ms-violation and timeout rates were **not directionally stable across
> replicates.** EATT is therefore a **tail-distribution tradeoff, not a demonstrated safety-budget mitigation.**

## Conclusion (most cautious defensible reading)
- **Severe tail — replicates.** The `>100 ms-or-timeout` rate is lower with EATT at saturation, per
  replicate (ON 65.9 / 66.7 % vs OFF 73.0 / 82.0 %). But the benefit is **negligible at low–mid load**
  (within ~1 point at 50–100 KB/s; at 50 KB/s EATT is even *slightly worse*, 6.6 vs 6.4 % pooled). Do
  **not** say "lower at every load."
- **Completed-sample median — replicates directionally.** EATT's saturated median is below OFF's ~193 ms
  mode: **replicate medians ON 141 / 152 ms vs OFF 193 / 193 ms** (the "146 ms" pooled figure is a
  **median of the two run medians**, `median([141,152])`, *not* a pooled sample median).
- **30 ms-violation & timeout rates at saturation — NOT directionally stable.** EATT generally increased
  `>30 ms-or-timeout` violations **through 125 KB/s**, but at 150 KB/s the OFF arm varies enough that the
  direction reverses (OFF replicates 80.3 % / 90.6 %; timeouts 5.1 % / 16.9 %) — so "EATT worsens the
  30 ms and timeout rates at every load" is **too strong at saturation**, and the 25 KB/s timeout claim
  is literally false (both ≈0).
- **Neither is remotely acceptable for a 30 ms deadline at saturation** (both fail >85 % pooled).
- **Net:** a **tail-distribution tradeoff, not a demonstrated safety-budget mitigation.** With EATT the
  distribution is **consistent with reduced bearer-level head-of-line blocking**, but bearer assignment
  is **unmeasured** (mechanism inferred, not shown). The only mitigation *measured* to hold a 30 ms
  budget under saturation is **completion-pacing** (`../../latency-under-load-20260813/`, coc §11.2).

## CoC-channel-vs-EATT — open
Raw CoC's separate channel held ~33 ms under near-saturated bulk (coc §11.3, **n=1**), far better than
EATT's ~146 ms saturated median here. That single point is not matched closely enough to establish
equivalence or CoC superiority — a matched, percentile/soak CoC-channel-vs-EATT experiment is needed.

## Provenance
`firmware/` holds all four flashed builds (hex + `.config`, both endpoints) with `SHA256SUMS`; both
serial streams for every arm are archived (`p2_{on1,off1,off2,on2}_{cen,per}.log`); driver
`eatt-rerun2.sh` (preregistered gate + analysis inline), run log `eatt-rerun2.log`.
