# Duplex whole-exchange model: second pre-registered test (2026-10-04)

**Status: RESOLVED — the model's exchange counts held at all four intervals whose prediction did not depend
on the exact end-of-event reserve; the one knife-edge interval (11.25 ms) narrowed the reserve to
250 < M < 312 µs.** GATT echo duplex, open Zephyr, on-chip exchange counter on (`--diag`). Predictions
were written to `PREDICTIONS.md` before any run at these intervals; the model and M = 250 µs were unchanged
since `duplex-fsu-followups-20261003`.

Model: full exchanges per event n = ⌊(T − M) ÷ (2·1048 µs + 2·g)⌋, g = 150 µs (FSU off) / 52 µs (on);
predicted gain = n_on ÷ n_off − 1.

| interval | predicted exchanges / gain | measured exchanges (mode, every rep) | measured gain, no held gate (n=4/4) | verdict |
|---|---|---|---|---|
| 11.25 ms | 4 → 5 / +25% (knife-edge: needs M ≤ 250 µs) | **4 → 4** | −0.2% ± 6.0% | miss: M > 250 µs |
| 13.75 ms | 5 → 6 / +20% | **5 → 6** | **+21.0% ± 3.3%** (158 → 192 KB/s) | pass |
| 16.25 ms | 6 → 7 / +16.7% | **6 → 7** | **+19.9% ± 5.8%** (157 → 188) | pass |
| 17.5 ms (control) | 7 → 7 / 0% | **7 → 7** | +5.4% ± 7.7% (no clear gain) | pass |
| 18.75 ms | 7 → 8 / +14.3% | **7 → 8** | +19.6% ± 10.8% (RF-degraded block) | pass (counts) |

- **The model's structure holds.** Large gains appeared exactly where predicted between intervals with
  ~0% gain (13.75 and 16.25 ms, between 12.5 and 15 ms and between 15 and 17.5 ms), and the control
  interval stayed flat.
- **11.25 ms** sits on the boundary: a 5th FSU-on exchange fits only if M ≤ 250 µs. It did not fit, so
  M is slightly above 250 µs. With the earlier bounds, **250 < M < 312 µs**. No earlier prediction changes
  anywhere in that range (20 ms needs M > 200; 22.5 ms needs M ≤ 500).
- **Throughput gain tracks the mean, not the mode.** Measured gains sit 1–5 points above the model where
  FSU-off events more often ended short of the full count (e.g. 16.25 ms: means 5.43 → 6.59 vs mode 6 → 7).
- **RF degraded during the 18.75 ms block:** the share of events reaching the full count fell from ~0.88
  (all earlier intervals) to 0.65, rates fell to 115–148 KB/s, and the FSU-held gate rejected 3 of 4
  FSU-on reps. Exchange counts were still exactly as predicted in every rep. Cause not identified (boards
  at the wider 2026-10-04 placement; no change was made to the rig).
- Held-gate sensitivity (`held-sensitivity.txt`, via `replication-20261004/held-sensitivity.py`): the
  tool's summary (`summary.txt`) applies the gate to FSU-on reps only; the table above uses all reps, since
  the gate here mostly caught RF drift. Verdicts are the same either way (with the gate, 18.75 ms keeps only one FSU-on rep, too few for a delta; its exchange counts are unaffected).

Files: `PREDICTIONS.md` (written before the run), `run.sh`, `results.jsonl`, `caps/` (central + peripheral
logs incl. `RPT pe:` exchange histograms), `firmware/` (+ SHA256SUMS; `p-echo.hex` identical to the
2026-10-03 archives), `configs/`, `summary.txt`, `held-sensitivity.txt`. Design gate MATCHED at every
interval. Local paths scrubbed with `tools/scrub-paths.py`.

Rig: 2× nRF54L15-DK (central 1057719509, peripheral 1057794857), boards further apart than 2026-10-03
(the `replication-20261004` placement); open `v4.4.2-16-gfee9fbc`.
