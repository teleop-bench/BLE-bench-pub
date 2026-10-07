# The CoC sink's auto-update moved the August "7.5 ms" head-to-head off 7.5 ms (2026-10-05)

**Status: RESOLVED (confirms a confound; quarantines the August CoC head-to-head cells).** The August
`sdc-vs-open-headtohead-20260814` CoC cells used sinks without `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`; the CoC sink
prefers a 50 ms interval (`PREF_MIN/MAX_INT=40`). Its "SDC CoC 7.5 ms" log jumps from ~121 to ~146 KB/s about 5 s in,
and its measurement window began at 22 s. This test isolates that one variable.

## Test (`au_test.py`, `results.json`)
- Central: SDC CoC, FSU on, 7.5 ms (the `oneway-crossstack-20261005` "max" image). Sinks: identical SDC `coc-sink`
  images except `BT_GAP_AUTO_UPDATE_CONN_PARAMS` (`matched-pair.txt`: MATCHED, only that symbol differs).
- Order: auto-update on, off, on, off; 40 s captures from boot; per-second receiver throughput.

| rep | sink | per-second KB/s, first 16 s | mean from 15 s |
|---|---|---|---|
| 1 | auto-update **on** | 56 38 125 124 125 124 **154 184** 177 177 183 … | **180.0** |
| 2 | auto-update off | 34 126 125 125 125 126 125 126 125 … | 125.1 |
| 3 | auto-update **on** | 31 125 126 125 126 **161 179** 184 182 183 … | **180.5** |
| 4 | auto-update off | 38 126 126 125 126 125 126 125 … | 125.7 |

**With auto-update on, throughput steps from ~125 to ~180 KB/s 4–6 s after connecting, the same timing as the
August log; with it off it stays at ~125 KB/s.** ~180 KB/s matches SDC CoC at 50 ms with FSU in the same-session
campaign (182.9), and ~125 matches SDC CoC at 7.5 ms (125.5). The interval change itself isn't logged by these
apps; the single-variable design makes the sink's auto-update the only possible cause.

## Consequence
- The August CoC cells (7.5 ms "parity" 146 vs 146; 12.5 ms "open +19.7%", ABBA×2) were not measured at their
  nominal intervals for most of each capture: **quarantined**. Its GATT/50 ms cell is unaffected in interval.
- The cross-stack reference is `oneway-crossstack-20261005` (auto-update verified off on every sink).

Files: `au_test.py`, `results.json`, `caps/`, `firmware/` (+ SHA256SUMS), `configs/`, `matched-pair.txt`. Rig: 2×
nRF54L15-DK at the `oneway-crossstack-20261005` placement; SDC NCS v3.4.0.
