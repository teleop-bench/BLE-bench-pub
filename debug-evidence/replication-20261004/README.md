# Replication session: duplex FSU + latency under load with FSU (2026-10-04)

**Status: RESOLVED — every qualitative duplex and 7.5 ms latency result from 2026-10-03 replicated** on a
different day with a different placement (the user moved the two nRF54L15-DKs further apart before the
session). **Same firmware:** every image built for this session (35 hexes, open and SDC) is
byte-identical to the image archived on 2026-10-03 (`firmware-*/SHA256SUMS` vs `gatt-duplex-fsu-matched`,
`gatt-duplex-fsu-model`, `coc-duplex-fsu-matched`, `latency-fsu`). Absolute rates are 3–8% lower today;
compare deltas, not absolutes.

## Duplex FSU gain (aggregate KB/s, FSU off → on, Welch 95%; n=4 per arm unless noted)
| cell | 2026-10-03 (close) | **2026-10-04 (further apart)** | replicates? |
|---|---|---|---|
| GATT open 7.5 ms | +0.3% ± 1.0% | +0.1% ± 3.2% (179.6 → 179.8) | yes (no gain) |
| GATT open 15 ms | 0% | −0.8% ± 1.2% (177.5 → 176.0) | yes (no gain) |
| **GATT open 25 ms** | +9.5% ± 2.1% | **+10.5% ± 4.9%** (171.5 → 189.5, n=4/3) | yes |
| GATT SDC 7.5 ms | +0.3% ± 0.9% | +1.3% ± 2.6% (178.0 → 180.3, n=4/3) | yes (no clear gain) |
| GATT SDC 15 ms | −0.2% ± 1.5% | +1.8% ± 5.4% (169.0 → 172.0) | yes (no clear gain) |
| **GATT SDC 25 ms** | +10.8% ± 0.2% | **+9.3% ± 4.5%** (168.2 → 183.8, n=4/3) | yes |
| CoC open 7.5 ms | +7.7% ± 1.1% | +7.6% ± 7.1% (164.6 → 177.1, n=4/2) | yes |
| CoC open 15 ms | +7.5% ± 1.1% | +10.5% ± 4.6% (157.6 → 174.2) | yes |
| **CoC open 25 ms** | +12.0% ± 0.5% (n=8) | **+11.6% ± 2.1%** (155.1 → 173.1, n=4/3) | yes |
| CoC SDC 7.5 ms | −0.2% ± 1.2% | +0.4% ± 0.8% (172.3 → 173.1) | yes (no gain) |
| CoC SDC 15 ms | +3.9% ± 2.4% (n=8) | +2.0% ± 0.9% (169.6 → 173.1) | yes (small gain) |
| **CoC SDC 25 ms** | +16.2% ± 2.6% (n=4/3) | **+9.6% ± 3.7%** (163.8 → 179.6) | gain yes; size smaller |

- The whole-exchange picture holds: GATT echo and SDC CoC gain nothing at 7.5/15 ms and ~+10% at 25 ms;
  open CoC gains at every interval with the same uplink-heavy split (today dl/ul ≈ 51–58 / 104–119 KB/s).
- SDC CoC 25 ms: +16.2% (10-03, 3-rep FSU-on block) vs +9.6% today. Quote it as "about +10–16%".
- Rejects (6 of 96 duplex reps): all FSU-on, all the FSU-held gate (sustained rate drop); no CoC stalls.

## Held-gate sensitivity (`held-sensitivity.py`, `held-sensitivity.txt`)
Both duplex tools apply the "FSU held" gate to the FSU-on arm only (to catch an FSU revert), so an RF dip
can drop an FSU-on rep while an FSU-off rep with the same dip stays in — a possible upward bias.
Re-measuring every tool-based duplex cell from both sessions with (a) no held gate and (b) the gate on
both arms moved the gain by at most ~1 point (largest: 10-04 CoC open 7.5 ms +7.6% → +6.4% with no gate);
no cell changed verdict. (Not covered: the harness-based `gatt-duplex-fsu-matched-20261003` run. The ±
in that file is the script's own Welch interval and can differ from the tools' summaries at small n;
compare the gains.)

## Latency under load, FSU off → on (`latency/`, n=2 per arm; ramp 0–150 KB/s, 10-buffer queue)
| config | p99 at 25 / 75 / 150 KB/s (ms) | p50 at 150 KB/s | idle p99 | bulk delivered at the 150 stage (off / on) |
|---|---|---|---|---|
| 7.5 ms naive | 38.5→32.5 / 42.0→35.0 / 46.0→39.5 | 33 → 26 | 19 → 19 | 141 / 149 |
| 7.5 ms paced | 25.5→20.5 / 28.0→24.0 / 33.5→25.5 | 19 → 15 | 19 → 19 | 137 / 149 |
| 25 ms naive | 71→71 / 72→71 / 69.5→61 | 30.5 → 25 | 71 → 71 | 141 / 149 |

- **7.5 ms (naive and paced): replicates** — FSU lowers loaded p99 by ~4–8 ms and the median by 4–7 ms;
  idle unchanged; no timeouts. Pacing remains the larger lever (paced FSU-off beats naive FSU-on).
- **25 ms: p99 is pinned at ~71 ms in both arms, idle included** — at this placement the tail is set by
  missed connection events (46 ms median + one 25 ms interval ≈ 71 ms), so the FSU p99 gain seen on 10-03
  (−4 to −7 ms) is masked except at 150 KB/s (69.5 → 61). The median still drops 5 ms under load.
- **Lower FSU-off ceiling at this placement:** at the 150 KB/s stage FSU-off delivered 137–141 KB/s while
  FSU-on still delivered 149, so the top stage is not like-for-like in bulk; compare 25–125 KB/s.
- Absolute RTTs are higher than on 10-03 (e.g. 7.5 ms naive p99 at 150 KB/s 39.5 → 46.0 ms FSU-off), as
  expected with more distance; the deltas are what transfer.

## Method
Same tools and recipes as 2026-10-03, fresh builds (`chain6.sh` = exact order): `tools/gatt-duplex-fsu.py`
(open, SDC; 7.5/15/25 ms), `tools/coc-duplex-fsu.py` (open, SDC; first connection discarded, peripheral-
only resets), `tools/latency-fsu.py` (7.5 ms naive + paced, 25 ms naive; one ABBA block). All design gates
MATCHED; per-rep gates as documented in REPRODUCE.md. Placement: further apart than 10-03 (distance not
measured — a placement change, not a calibrated range point).

Files: `gatt-open/`, `gatt-sdc/`, `coc-open/`, `coc-sdc/`, `latency/` (`results.jsonl` + `caps/`),
`firmware-b-*/` (+ SHA256SUMS) and `configs-b-*/`, `summaries.txt` (tool summaries), `held-sensitivity.*`,
`chain6.sh`. SDC images are covered by THIRD-PARTY-NOTICES.md. Local paths scrubbed with
`tools/scrub-paths.py`.

Rig: 2× nRF54L15-DK (central 1057719509, peripheral 1057794857); open `v4.4.2-16-gfee9fbc`, SDC NCS v3.4.0.
