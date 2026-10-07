# Second-day replication of the corrected CoC results (2026-10-07)

**Status: REPLICATED — every cell kept its verdict by the criteria pre-registered the evening before
(`PREDICTIONS.md`).** Unattended run started 06:45 (`overnight.sh`, `overnight.log`), boards not moved since 2026-10-06
(close range, ~3–4 in). Rebuilt from a pinned worktree of commit `8af4a95`; all 36 images are byte-identical to
the 2026-10-06 runs (`coc-duplex-fsu-q20-20261006`, `coc-duplex-fsu-fixed-20261006`, `oneway-coc-batched-20261006`). 128/128 reps accepted, 0 stalls.

Criterion: a cell fails if its FSU gain moves by more than its 2026-10-06 95% interval + 2 points, or a duplex split
leaves ~1:1.

## CoC duplex (aggregate KB/s, n=4 per arm; `coc-open/`, `coc-sdc/`)
| interval | Zephyr 10-06 → 10-07 gain | Zephyr 10-07 off → on | SDC 10-06 → 10-07 gain | SDC 10-07 off → on | verdict |
|---|---|---|---|---|---|
| 7.5 ms | +0.2% ± 0.1 → +0.1% ± 0.2 | 184.2 → 184.3 | +0.0% ± 0.3 → +0.1% ± 0.3 | 184.2 → 184.4 | replicated |
| 15 ms | +2.5% ± 2.3 → +2.7% ± 1.6 | 185.4 → 190.5 | +2.8% ± 0.3 → +3.0% ± 0.2 | 184.4 → 189.9 | replicated |
| 25 ms | +9.6% ± 0.6 → +9.8% ± 0.4 | 186.8 → 205.1 | +10.2% ± 0.5 → +11.3% ± 2.5 | 185.2 → 206.1 | replicated |

Every split ~1:1 (e.g. Zephyr 25 ms on 101.7 / 103.4, SDC 25 ms on 102.2 / 103.9). Aggregates within ~1.5 KB/s of 10-06.

## One-way CoC, batched credit returns (KB/s, n=4 per cell; `oneway-coc/summary.txt`)
| interval | Zephyr gain 10-06 → 10-07 | Zephyr 10-07 off → on | SDC gain 10-06 → 10-07 | SDC 10-07 off → on | verdict |
|---|---|---|---|---|---|
| 7.5 ms | +20.0% ± 1.0 → +19.6% ± 1.1 | 155.6 → 186.1 | +25.3% ± 0.4 → +25.4% ± 0.3 | 124.6 → 156.2 | replicated |
| 15 ms | +19.8% ± 0.6 → +20.1% ± 0.3 | 155.5 → 186.7 | +10.0% ± 0.7 → +10.3% ± 0.6 | 155.4 → 171.4 | replicated |
| 25 ms | +16.0% ± 0.1 → +15.7% ± 0.7 | 167.8 → 194.2 | +11.8% ± 1.0 → +11.9% ± 1.2 | 158.2 → 177.1 | replicated |
| 37.5 ms | +14.9% ± 0.8 → +12.3% ± 7.4 | 165.1 → 185.3 | +15.4% ± 1.5 → +15.9% ± 2.6 | 160.0 → 185.3 | replicated (Zephyr: 2.6-point move within the 2.8 allowance; noisy) |
| 50 ms | +14.5% ± 0.9 → +15.3% ± 4.9 | 162.6 → 187.6 | +14.6% ± 0.7 → +15.7% ± 3.5 | 155.7 → 180.1 | replicated (noisy) |

SDC 15 ms FSU off 155.4 vs Zephyr 155.5: the CoC parity at 15 ms replicates. Zephyr − SDC at 7.5 ms: +31.0 (off).

## Notes
- **Long intervals were noisier this morning.** Five one-way reps at 37.5 / 50 ms read 8–25 KB/s low (Zephyr 37.5 ms
  rounds 1 and 3; Zephyr 50 ms round 4; SDC 50 ms rounds 3–4, both arms); all passed every gate and are kept per
  protocol. Same signature as the single radio-dip rep on 10-05: long events are more exposed to packet loss. The
  paired per-round gains still replicate; the absolute 37.5 / 50 ms rates should be read as ±10 KB/s day to day.
- Same placement as 10-06, so this replicates across days (RF environment, time of day), not across distance.
- Logged with the pre-gate tools of `8af4a95` (the runtime link gates of `tools/link_gates.py` came after).

Files: `PREDICTIONS.md`, `overnight.sh`, `overnight.log`, `coc-open/`, `coc-sdc/`, `oneway-coc/` (results.jsonl, caps,
summary, design gate), `*.log`, `firmware/` (+ SHA256SUMS; SDC images listed in THIRD-PARTY-NOTICES.md), `configs/`.
