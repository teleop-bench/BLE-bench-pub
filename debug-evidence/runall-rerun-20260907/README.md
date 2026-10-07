# run-all.sh full re-run — 2026-09-07 (post config-audit)

One-pass reproduction of the 8-config headline suite via the **fixed** `run-all.sh` (captures from boot,
so the `fsu52` column now actually verifies FSU engagement). 2× nRF54L15-DK, one 40 s measurement per
config (n=1 per cell → RF-noisy; the campaign dirs are the higher-confidence source for absolute numbers).
This run's job was to (a) dogfood the run-all FSU-verify fix and (b) confirm the audit didn't break the story.

## Result (`table.txt` / `results.tsv`)
| transport | stack | fsu | KB/s | sanity |
|---|---|---|---|---|
| GATT | open | on  | 189   | PHY2M **fsu52** |
| GATT | open | off | 157   | PHY2M |
| GATT | SDC  | on  | 177   | PHY2M |
| GATT | SDC  | off | 155   | PHY2M DLE251 |
| CoC  | open | on  | 157.1 | PHY2M DLE251 **fsu52** |
| CoC  | open | off | 158.0 | PHY2M DLE251 |
| CoC  | SDC  | on  | 178.2 | PHY2M DLE251 |
| CoC  | SDC  | off | 152.7 | PHY2M DLE251 |

Deltas: FSU GATT/open **+20.4%**, FSU GATT/SDC **+14.2%**, FSU CoC/SDC **+16.7%**, FSU CoC/open **−0.6%**;
open-vs-SDC parity +1.3% (GATT) / +3.5% (CoC).

## What it confirms
- **The run-all FSU-verify fix works** — `fsu52` populates for the open FSU-on arms (proof logs in
  `captures/`: `Q3FSU-DONE … spacing=52`, `FSU: updated … spacing=52 us`).
- **Open ≈ SDC parity** holds (within a few %).
- **FSU helps GATT** (~+14–20%) and is **regime-dependent on CoC** — CoC/open at 15 ms shows ~0%
  (157 vs 158) *even with FSU verified engaged*, because CoC saturates ~157 KB/s at 15 ms (airtime
  headroom, and thus the FSU gain, appears at 25–50 ms). This matches the documented story.

## Caveat
n=1 per cell — these are reproduction/sanity numbers, RF-day specific. Do NOT overwrite the campaign
figures (or the founder-facing aggregate) with these; the multi-round campaign dirs remain the source
of record for absolute KB/s. The transferable result is the *shape* (parity; FSU helps GATT; FSU
regime-dependent on CoC), which reproduces.
