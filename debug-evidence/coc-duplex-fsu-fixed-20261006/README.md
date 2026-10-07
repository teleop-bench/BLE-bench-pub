# CoC duplex FSU with the credit bugs fixed, both stacks (2026-10-06)

**Status: RESOLVED, except the Zephyr 25 ms cell (provisional: its FSU-off rate is unexplained).** Supersedes the CoC
duplex cells of `coc-duplex-fsu-matched-20261003`, `duplex-fsu-followups-20261003` and `replication-20261004`, which
were measured with the CoC credit bugs described in `coc-credit-fixes-20261006`. Prediction in `PREDICTIONS.md`
(written before the run).

## Design
`tools/coc-duplex-fsu.py` unchanged in procedure (matched on/off centrals, held-FSU gate on the on arm, first
connection after each central flash discarded, peripheral-only resets, ABBA ×2 → n=4 per arm), run on the fixed
sources: credit counters reset per connection, `rx.credits` zeroed before connect/accept, the central's 64-credit
window sent in the connection request. Recipe queues (64-deep on both sides), 480 B downlink SDUs, 244 B uplink SDUs.
Rates: both directions from cumulative byte counters (downlink no longer from the ~1% high per-second line).
Open `v4.4.2-16-gfee9fbc`; SDC NCS v3.4.0. Close range, same placement as `oneway-crossstack-20261005`.

## Result (`open/results.jsonl`, `sdc/results.jsonl`; aggregate KB/s, Welch t95; 48/48 accepted, 0 stalls)
| interval | Zephyr off → on | gain | SDC off → on | gain |
|---|---|---|---|---|
| 7.5 ms | 183.1 → 183.8 | +0.3% ± 0.3 | 183.9 → 183.9 | +0.0% ± 0.3 |
| 15 ms | 182.6 → 182.8 | +0.1% ± 0.4 | 184.3 → 189.3 | +2.8% ± 0.3 |
| 25 ms | 154.5 → 191.2 | **+23.8% ± 0.7 (provisional)** | 186.3 → 205.4 | +10.2% ± 0.5 |

Per direction (downlink / uplink): every cell is ~1:1 (e.g. Zephyr 7.5 ms off 90.8 / 92.3, SDC 25 ms on 101.8 / 103.5).

## Reading
- **Both stacks now split evenly**, so the open CoC link no longer mixes duplex and one-way events. The published open
  gains (+7.7 / +7.5 / +12.0%, split ~1:2) were produced by the credit bugs.
- **7.5 and 15 ms: no FSU gain** on Zephyr (+0.3 / +0.1%) and none / small on SDC (+0.0 / +2.8%), the GATT duplex
  pattern predicted by the whole-exchange model (3 → 3 and 6 → 6 exchanges per event). SDC 25 ms +10.2% matches the
  model's ~+10% (10 → 11).
- **Zephyr 25 ms is not explained.** FSU on (191.2) is consistent with ~10 exchanges per event, but FSU off (154.5) sits
  far below SDC (186.3) and below Zephyr's own 7.5 / 15 ms rates (~183), as if ~2 exchanges per event were lost at 150 µs
  gaps. Reproduces exactly: `coc-credit-fixes-20261006` cold reps read 78 / 78 at 25 ms FSU off. Not predicted
  (prediction: ~+10%). Until the per-event count is measured (on-air observer or the credit trace at 25 ms), treat the
  +23.8% as provisional; it may be an open-controller or app effect specific to 25 ms with FSU off.
- One session, n=4 per arm; not yet replicated on a second day.

Files: `PREDICTIONS.md`, `open/` and `sdc/` (results.jsonl, caps/, run.log), `firmware-open/`, `firmware-sdc/`
(+ SHA256SUMS; SDC images listed in THIRD-PARTY-NOTICES.md), `configs-open/`, `configs-sdc/`.
