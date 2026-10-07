# Benchmark on the latest Zephyr release — v4.4.1 vs v4.4.2 (2026-09-07)

Currency/regression check: **does the benchmark reproduce on the latest Zephyr release?** Our fork was
`fsu-m0` on `v4.4.1`; this rebases the 16 fsu-m0 commits onto **v4.4.2** (clean — 0 conflicts; v4.4.2's
197 commits touch none of our patched files) and A/Bs the two, same RF day.

## Viability (already established)
- **Rebase clean:** `fsu-m0` (v4.4.1+16) → `v4.4.2` replayed all 16 commits, no conflicts →
  branch **`fsu-m0-v442`**, pushed to `github.com/teleop-bench/zephyr`.
- **Builds clean:** `coc-central` + the CoC-open set build on v4.4.2 after `west update`.
- So the benchmark **ports to the latest release with zero code changes** — that alone is the headline
  "runs on current upstream" answer.

## The A/B run (`harness.py`)
Alternating arms per round (drift cancels): CoC one-way open throughput @15 ms, {FSU on, FSU off},
on both **v4.4.1** (`prebuilt-hexes/`) and **v4.4.2** (`firmware/`, this dir). Compares absolute KB/s
and the transferable **FSU on/off delta** across many rounds. Live: `summary.txt`; per-measurement:
`results.jsonl`. Scope = the **open** controller (SDC is Nordic's separately-versioned controller —
not part of the "latest Zephyr" question).

## Saved artifacts
- `firmware/` — the v4.4.2 hexes + `.config` + `SHA256SUMS` (v442-coc-cen-{fsu,off}, v442-coc-sink).
- Fork branch `fsu-m0-v442` (rebased patches) on `teleop-bench/zephyr`.

## ⚠️ First A/B run INVALIDATED (2026-09-07) — re-run required
The initial 168-round A/B (`summary.txt`/`results.jsonl`/`caps/`, since removed) reported **FSU on/off
delta = 0.0% on BOTH v4.4.1 and v4.4.2** (~160.5/160.6 KB/s). That is **not** a valid FSU result: the
v4.4.2 arm was flashed with the **pre-fix `coc-sink` firmware that was missing the FSU floor**
(`CONN_INTERVAL_LOW_LATENCY`), so it negotiated `spacing=150` and FSU never engaged — see
`debug-evidence/fsu-config-audit-20260907/`. The v4.4.1 arm's 0% is a separate effect (CoC saturates
~160 KB/s at 15 ms, leaving no FSU headroom — FSU shows at 25–50 ms, not 15 ms). The `firmware/v442-coc-sink`
here has since been **rebuilt with the floor** (verified `spacing=52`); the A/B must be re-run against it.

## Follow-ups (when the run completes)
1. **If v4.4.2 proves out** (deltas within RF scatter of v4.4.1): promote it — rename the fork so
   **`fsu-m0` = v4.4.2** (default) and the old v4.4.1 line becomes **`fsu-m0-v441`**; update the
   `west init --mr fsu-m0` recipe in README/REPRODUCE to pull v4.4.2, and add a LESSONS/EVIDENCE-INDEX
   note. (Per the user's plan — contingent on this A/B.)
2. Write FINDINGS here with the numeric result; add the row to `docs/EVIDENCE-INDEX.md`.
