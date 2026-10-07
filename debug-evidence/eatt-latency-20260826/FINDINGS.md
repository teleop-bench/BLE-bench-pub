# EATT latency-under-load A/B

> ## ★ CANONICAL result = `preregistered-20260827/FINDINGS.md` (preregistered-gate PASS,
> ## publication-grade). Read that. It supersedes both runs below.
> Lineage: **(1)** 2026-08-26 first run — **INVALID** (seq-correlation defect, kept at bottom, do not
> cite). **(2)** `rerun-20260827/` matching-pong rerun — valid *by source semantics via a documented
> post-hoc gate deviation* (this section). **(3)** `preregistered-20260827/` — corrected gate declared
> up front and **passed** → the canonical number. The two valid runs agree in *direction* (EATT worsens
> the 30 ms-budget rate at every load; lowers the saturated median — 106 ms then 146 ms; severe-tail
> benefit only near saturation) with RF-day-varying magnitude. **Bottom line: EATT is a saturation-only
> tail-reshaper, not a 30 ms budget fix.**

# (2) matching-pong rerun 2026-08-27 (rerun-20260827/) — valid by source semantics, documented gate deviation

> ## Status: VALID BY SOURCE SEMANTICS, with a documented post-hoc gate deviation.
> **First run invalid:** `notify_cb` woke the RTT waiter on *any* notification → EATT's multi-bearer
> reorder contaminated 50–76 % of samples. **Fixed** (`eattlat`/`z54-lat` `notify_cb`: wake only on
> matching seq + per-probe `k_sem_reset`).
>
> **Validation-gate deviation (disclosed honestly):** the archived runner **preregistered
> `seqbad==0 ⇒ INVALID`** and, on that rule, **flagged all four arms invalid** (`seqbad` ON1/OFF1/OFF2/ON2
> = 63/14/29/79). That gate was **overly strict**: with the fixed callback, a *correctly-discarded* late
> reply — one arriving after its probe already timed out — still increments `seqbad`. A **source + log
> audit** confirms the recorded RTT samples always matched the outstanding sequence: `seqbad=0` before
> the first timeout in every arm, and cumulative `seqbad ≤ cumulative timeouts` throughout, and the
> corrected callback never records a mismatched pong. So this result is **valid by source semantics, via
> a post-hoc gate correction** — *not* by the number the preregistered gate printed. For a
> publication-grade final number, use the **corrected gate, preregistered, and re-run** (below).
> **Corrected gate (for the rerun / future):** (a) `seqbad=0` before the first timeout; (b) every
> recorded RTT is an exact sequence match; (c) cumulative `seqbad ≤ cumulative timeouts`; (d) rename the
> counter **`late_pong_dropped`**, not generic `seqbad`. ABBA order (ON/OFF/OFF/ON, reset-isolated)
> **reduces run-order drift** (on1≈on2, off1≈off2). *Bearer assignment is not instrumented.*

## Result — TIMEOUT-INCLUSIVE (timeouts counted as budget failures, in the denominator)
p99 is **omitted**: the 200 ms ping timeout censors exactly the >200 ms region, so a "p99≈200 ms" is a
censoring artifact, not a true p99. Pooled ON=on1+on2 vs OFF=off1+off2.

| load KB/s | ON >30 ms-or-timeout | OFF | ON >100 ms-or-timeout | OFF | ON median | OFF median | ON timeout% | OFF timeout% |
|-----------|------|-----|------|-----|-----------|------------|-------------|--------------|
| 25   | 6.9%  | 4.6%  | 1.5%  | 2.5%  | 26  | 26  | 0.0% | 0.0% |
| 50   | 10.6% | 9.4%  | 6.1%  | 6.7%  | 26  | 26  | 0.3% | 0.1% |
| 75   | 18.6% | 14.5% | 9.8%  | 10.4% | 26  | 26  | 1.3% | 0.4% |
| 100  | 27.9% | 25.4% | 17.6% | 19.7% | 26  | 26  | 2.3% | 0.8% |
| 125  | 45.1% | 40.8% | 31.7% | 35.2% | 26  | 26  | 4.1% | 1.1% |
| 150* | 85.3% | 80.4% | 61.1% | 72.8% | **106** | **193** | 6.8% | 3.7% |

*saturation. This run's pooled figures (median 106 ms here) are **this predecessor run's data**.

## Interpretation — SUPERSEDED by the canonical run (`preregistered-20260827/`)
> This section's original bullets stated "EATT shows a lower severe tail **at every load**" and
> "EATT **worsens** the 30 ms & timeout rates **at every load**" — both **over-strong** and corrected in
> the canonical run: the severe-tail benefit **replicates only near saturation** (negligible at low–mid
> load — at 50 KB/s EATT is even slightly worse), and the saturated **30 ms/timeout directions are NOT
> stable across replicates**. The saturated "median" is a **median of run medians** (this run 106 ms; the
> canonical run 141/152 ms). **Read `preregistered-20260827/FINDINGS.md`.** Direction that survives both
> valid runs: severe tail lower at saturation; median below the ~193 ms OFF mode; neither meets a 30 ms
> deadline — **a tail-distribution tradeoff, not a safety-budget mitigation.**

---
*(Below: the INVALID 2026-08-26 first run, retained for the record only — do not cite.)*
# EATT latency-under-load A/B (2026-08-26, INVALID)


Rig: 2× nRF54L15-DK, open Zephyr v4.4.1+fsu-m0, GATT ping-pong (write→notify) stop-signal on an
encrypted link while a bulk write-load ramps 0→150 KB/s (7×60 s stages). Apps: `eattlat-central`
(driver, `bt_conn_set_security L2` + per-second `eatt=N`) + `eattlat-periph` (echo + bulk sink).
15 ms interval, 2M, DLE 251. EATT-ON = 4 ECRED bearers (verified `eatt=4`); EATT-OFF = same
encrypted link, single ATT bearer. Bulk payload 240 B (fits the 245 EATT bearer MTU — see the
`-6` MTU gotcha). Clean capture (0 `<err> bt_l2cap`). Metric: per-stage RTT percentile histogram.

## Result (p99 ms / >100 ms rate / >30 ms rate)
| load KB/s | EATT-ON | EATT-OFF |
|-----------|---------|----------|
| 0    | 26 / 0% / 0.7%    | 26 / 0% / 0.6%   |
| 25   | 101 / 1.4% / 6.7% | 160 / 2.2% / 4.1% |
| 50   | 95 / 0.4% / 3.7%  | 159 / 2.8% / 5.5% |
| 75   | 104 / 1.7% / 7.2% | 188 / 4.8% / 10.9% |
| 100  | 105 / 2.6% / 13%  | 184 / 9.1% / 16% |
| 125  | 110 / 3.9% / 25%  | 190 / 15.6% / 25% |
| 150* | 139 / 19.5% / 76% | 190 / 32% / 55% |

*150 KB/s ≈ the airtime ceiling (saturation).

## Interpretation
- **Below saturation (25–125 KB/s): EATT helps.** It cuts the p99 tail by ~40–90 ms and roughly
  **halves the severe (>100 ms) rate**, at a comparable-or-lower >30 ms rate. Giving the stop-signal
  its own ATT bearer stops it from queueing behind the bulk on one bearer (L2CAP head-of-line relief).
- **At full saturation (150 KB/s): EATT stops helping and its median collapses** (p50 95 ms vs 31 ms;
  76% vs 55% over 30 ms). All 4 bearers share **one ACL connection's per-event airtime**; when airtime
  is 100% consumed the extra bearers can't manufacture free slots, and the ping's round-robin share
  actually starves (fewer pings delivered: 770 vs 888).
- **Net for a 30 ms safety budget:** EATT is a real mitigation for a bulk+control mix **up to ~80% of
  the throughput ceiling**, but it does NOT get the stop-signal under budget at saturation. Confirms
  the mechanism: EATT relieves L2CAP-queue head-of-line but **not** shared per-event airtime.

## Where this leaves the latency-under-load fix set (best → worst under saturation)
1. **Completion-pace the bulk** to shallow outstanding depth — keeps the queue AND airtime-bursts
   short (measured earlier: >30 ms 100%→2.5% at same throughput). Addresses depth AND airtime.
2. **Separate connection** — separate per-event airtime (hard bound; untested here).
3. **EATT / separate L2CAP bearer** — relieves queue head-of-line, bounds the extreme tail, but
   shares airtime → fails at saturation. Good below the ceiling.
4. **Don't saturate** — keep offered load under ~80% of ceiling and EATT keeps the tail modest.
