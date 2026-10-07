# Duplex FSU follow-ups: model test, CoC split, CoC on air, n=8 re-runs (2026-10-03)

**Status: RESOLVED (open stack unless noted; one session, one rig, close range).** Follow-ups to
`gatt-duplex-fsu-matched`, `gatt-duplex-fsu-model`, `coc-duplex-fsu-matched` and `onair-duplex`. The
latency-under-load FSU comparison run in the same session is archived separately.

## 1. Whole-exchange model: new intervals, then a pre-registered test (GATT echo, open, `--diag`)
Model: full exchanges per event n = floor((T − M) / t_exch), with t_exch = 2 × 1048 µs + 2 × gap
(2396 µs at 150 µs, 2200 µs at 52 µs) and an end-of-event reserve M ≈ 250 µs.

`model-test/` (10/20/30/50 ms). Predicted before running: 4→4, 8→9, 12→13, 20→22.
| interval | off → on KB/s (n=4 each) | FSU gain (Welch 95%) | exchanges/event (mode, on-chip counter) |
|---|---|---|---|
| 10 ms | 179.8 → 182.9 | +1.7% ± 2.2% (null) | 4 → 4 |
| 20 ms | 179.6 → 184.1 | +2.5% ± 4.6% (null) | **8 → 8** (predicted 8 → 9) |
| 30 ms | 174.2 → 197.6 | +13.4% ± 5.3% | 12 → 13 |
| 50 ms | 176.6 → 197.4 | +11.7% ± 2.8% | 20 → 22 |

20 ms missed: 19750/2200 = 8.98, so the 9th FSU-on exchange needs a reserve of at most 200 µs; with the
other counts this bounds 200 < M < 312 µs. The model was then fixed (M ≈ 250 µs) and **predictions for
three new intervals were written down before running them** (`PREDICTIONS-step7.md`).

`model-prereg/` (12.5/22.5/35 ms):
| interval | predicted exchanges / gain | measured exchanges (mode) | measured gain (n=4 each) |
|---|---|---|---|
| 12.5 ms | 5 → 5 / ~0% | **5 → 5** | +0.0% ± 0.0% (186.0 → 186.0) |
| 22.5 ms | 9 → 10 / ~+11% | **9 → 10** | **+10.6% ± 0.7%** (185.6 → 205.2) |
| 35 ms | 14 → 15 / ~+7% | **14 → 15** | **+6.9% ± 2.1%** (183.9 → 196.5) |

All three pre-registered predictions held, counts and gain size. Across 10 intervals (7.5–50 ms) GATT
echo duplex FSU gain is a sawtooth set by whole-exchange quantization, not a smooth function of interval.

## 2. Open CoC with an even split (`coc-cold/`, `--cold`: both boards reset every rep)
Goal: test whether the open stack's uneven split (~1:2 uplink-heavy, from resetting only the
peripheral) drives its CoC gain at 7.5/15 ms. With `--cold` every rep is a first-since-boot connection.
- **The reconnect wedge hit 11 of 16 reps** (uplink 0 KB/s): 7 of 8 at 7.5 ms, 4 of 8 at 15 ms, in both
  arms. Earlier rigs saw cold resets wedge only at ≥ 25 ms (`docs/LESSONS.md`); with these images it also
  wedges at 7.5/15 ms. `--cold` is not usable for CoC measurements.
- Reps that did not wedge split evenly (dl/ul ≈ 86/87). 15 ms: off 172.8 (n=2) → on 176.9 (n=2),
  **+2.4% ± 0.6%**, vs +7.5% with the uneven split (`coc-duplex-fsu-matched`). 7.5 ms: one accepted
  FSU-off rep (173.3), no FSU-on rep, no delta. Small n; consistent with the SDC CoC even split (below).

## 3. CoC on air (`onair-coc/`, `tools/onair-duplex.py --mode coc`, open, uneven split)
Same observer method as `onair-duplex`; event kinds added to the analyzer. The air shows two kinds of
connection event, which the GATT echo link never produces:
- **duplex events:** both sides send a full packet every exchange (`DDDD…`);
- **one-way events:** the central sends only empty packets and the peripheral sends data (`eDeD…`). An
  empty + full exchange takes ~1.2–1.4 ms instead of ~2.2–2.4 ms, so more exchanges fit.

| interval | duplex events: exchanges off → on | one-way events: exchanges off → on | one-way share (off / on) | tIFS on air |
|---|---|---|---|---|
| 7.5 ms | 3 → 3 | **5 → 6** | 41% / 29% | 150 / 52 µs |
| 15 ms | 6 → 6 | **10 → 12** | 31% / 27% | 150 / 52 µs |
| 25 ms | 10 → 11 | **17 → 20** | 36% / 34% | 150 / 52 µs |

(4 reps per interval, 2 per arm, 21–110 complete events per rep; `onair-coc/summary-reanalyzed.txt` from `--reanalyze`.) **This is the measured mechanism behind
the open CoC gain at every interval:** the one-way events gain 18–20% at every interval, and the duplex
events quantize like GATT echo. The same whole-exchange model with the same 250 µs reserve and an
empty-PDU exchange of 1048 + 44 µs + 2 gaps reproduces all 12 counts (post-hoc, no new parameters).
Why the link alternates between the two kinds (central TX queue running dry?) is not measured here.

## 4. n=8 re-runs of the two noisiest CoC cells
| cell | earlier (n=4) | **n=8 run** | notes |
|---|---|---|---|
| open CoC 25 ms (`coc-open25-n8/`) | +15.2% ± 4.3% (163.8 → 188.6) | **+12.0% ± 0.5%** (173.7 → 194.5, n=8/8) | 0 rejects, 0 stalls; split 57/117 → 64/131 |
| SDC CoC 15 ms (`coc-sdc15-n8/`) | +1.5% ± 5.5% (null) | **+3.9% ± 2.4%** (181.9 n=5 → 189.0 n=4) | 7 of the first 7 reps rejected (short window ×3, no reconnection ×2, no FSU completion ×2), then 9/9 accepted |

The open 25 ms gain is now pinned at +12%, between the earlier +15% and the GATT echo +10%. SDC CoC
15 ms is a small but non-zero gain (strict non-overlap), not a null. The SDC early-rep rejections were
link-setup failures caught by the gates; cause not pinned down.

## Files
`model-test/`, `model-prereg/`, `coc-cold/`, `onair-coc/`, `coc-sdc15-n8/`, `coc-open25-n8/` (each
`results.jsonl` + `caps/` logs); `firmware-b-model/`, `firmware-b-prereg/` (+ SHA256SUMS) and matching
`configs-*`; the CoC runs reused the archived `coc-duplex-fsu-matched-20261003` images (open and SDC);
the on-air CoC run used those open images and the observer image archived in `onair-duplex-20261003/firmware/` (`observer-2m.hex`, same SHA-256).
`summaries.txt` = the tool summaries; `chain*.sh` = the exact unattended run order. Local paths scrubbed
with `tools/scrub-paths.py`.

Rig: 2× nRF54L15-DK (central 1057719509, peripheral 1057794857) + nRF52832 DK observer (1050347760),
close range; open `v4.4.2-16-gfee9fbc`, SDC NCS v3.4.0.
