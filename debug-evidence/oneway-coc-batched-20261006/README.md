# One-way CoC FSU, Zephyr vs SDC, with batched credit returns (2026-10-06)

**Status: RESOLVED. Replaces the CoC half of `oneway-crossstack-20261005`** (which used the recipe sink that returns
one credit per segment; that policy costs exchanges and distorted CoC rates and gains, `coc-credit-policy-20261006`).
Prediction in `PREDICTIONS.md` (written before the run). The GATT half of `oneway-crossstack-20261005` is unaffected
and stays the GATT reference.

## Design
`tools/oneway-fsu.py --transports coc --coc-credit-batch --phase sweep`: identical to the 10-05 CoC sweep (same recipe,
"max" tuning on both stacks, design gates for matched pairs / tuning / host parity / sink capability, interleaved
rounds with Zephyr and SDC adjacent, both boards reset per rep, 32 s, window from FSU onset) except the CoC sinks
return credits in batches (`CONFIG_APP_CREDIT_BATCH=y`: one credit PDU per ~41 segments). KB/s from the sink's
cumulative byte counter (the per-second line read ~1% high). 80/80 reps accepted. Open `v4.4.2-16-gfee9fbc`;
SDC NCS v3.4.0; close range.

## Result (`summary.txt`, `results.jsonl`; n=4 per cell, Student-t 95%)
| interval | Zephyr off → on (gain) | SDC off → on (gain) | Zephyr − SDC, FSU off / on |
|---|---|---|---|
| 7.5 ms | 155.8 → 186.9 (+20.0% ± 1.0) | 124.6 → 156.2 (+25.3% ± 0.4) | +31.1 ± 0.5 / +30.7 ± 1.5 |
| 15 ms | 155.4 → 186.2 (+19.8% ± 0.6) | 155.9 → 171.5 (+10.0% ± 0.7) | −0.5 ± 0.7 / +14.7 ± 1.3 |
| 25 ms | 168.0 → 195.0 (+16.0% ± 0.1) | 158.6 → 177.3 (+11.8% ± 1.0) | +9.4 ± 1.2 / +17.6 ± 1.0 |
| 37.5 ms | 166.4 → 191.2 (+14.9% ± 0.8) | 161.0 → 185.9 (+15.4% ± 1.5) | +5.4 ± 2.7 / +5.3 ± 1.4 |
| 50 ms | 167.5 → 191.7 (+14.5% ± 0.9) | 162.3 → 186.0 (+14.6% ± 0.7) | +5.2 ± 1.0 / +5.7 ± 1.5 |

## What changed vs the per-segment sink (`oneway-crossstack-20261005`, which read ~1% high from the per-second line)
- **7.5 ms:** FSU gain +0.0% → **+20.0% (Zephyr) / +25.3% (SDC)**, matching GATT (+20.2% / +24.4%).
- **SDC 15 ms, FSU off: 141.0 → 155.9.** The per-segment sink cost SDC ~10% even without FSU at 15 ms, so the published
  CoC "Zephyr leads SDC by ~16 KB/s at 15 ms" (FSU off) is wrong: **parity (−0.5)**. GATT still shows the 15 ms lead.
- **SDC 25 ms gain: +18.5% → +11.8%;** Zephyr 25 ms FSU off 159.5 → 168.0 (+5%).
- 37.5 / 50 ms: rates within a few KB/s and gains within ~1.5 points of the per-segment values.
- Zephyr's lead at 7.5 ms (~31 KB/s) is unchanged; from 25 ms the stacks are within ~5–18 KB/s.
- The prediction held at 7.5 ms and 37.5 / 50 ms; it failed at 15 / 25 ms for SDC (gains lower, not higher, than the
  per-segment values, because the per-segment sink had depressed SDC's FSU-off rate more than its FSU-on rate).
- One session, n=4; not yet replicated on a second day.

Files: `PREDICTIONS.md`, `summary.txt`, `results.jsonl`, `design-gate.txt`, `run.log`, `caps/`, `firmware/` (+ SHA256SUMS;
SDC images listed in THIRD-PARTY-NOTICES.md), `configs/`.
