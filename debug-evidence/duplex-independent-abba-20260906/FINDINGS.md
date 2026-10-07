# Independent (non-echo) CoC-duplex, ±FSU, ABBA — FINDINGS (2026-09-06)

**Ran the scaffold on real hardware.** This resolves the two audit flags: (1) whether the published
"~90+90, ≤0.1% imbalance" duplex *symmetry* is real, and (2) whether duplex-FSU gives a gain.

## Setup / provenance
- 2× nRF54L15-DK: central `1057794857`, sink/periph `1057719509`. Open `ll_sw_split`, fork
  `teleop-bench/zephyr@fsu-m0` (`8f44a3d7a3f`). 2M + DLE-251 (verified per arm). One board pair,
  one RF setting (bench), single session. 60 s capture/arm, steady window = drop first 25%.
- Rig = **independent, non-echo**: central blasts downlink + counts uplink (`CENRX`); sink blasts
  uplink + counts downlink (`SINK rx`). Two separate counters (see README).
- Matrix: {25 ms, 50 ms} × {FSU on/off}, ABBA (on/off/off/on). Parsed with `tools/analyze.py`.
- Raw logs: `captures/*.log` (+ `run.log`). Downlink = sink `CoC throughput`; uplink = central
  `CoC uplink (CENRX)`.

## Result (KB/s)
| arm | downlink | uplink | aggregate | fsu |
|---|---|---|---|---|
| 25 ms FSU-off (r2) | 151.9 | **0** | 151.9 | 0 |
| 25 ms FSU-off (r3) | 152.7 | **0** | 152.7 | 0 |
| 25 ms FSU-on  (r1) | 140.9 | **0** | 140.9 | 52 |
| **25 ms FSU-on (r4)** | **76.9** | **78.1** | **155.0** | 52 |
| 50 ms FSU-off (r2) | 154.1 | **0** | 154.1 | 0 |
| 50 ms FSU-off (r3) | 152.4 | **0** | 152.4 | 0 |
| 50 ms FSU-on  (r1) | 152.5 | **0** | 152.5 | 52 |
| 50 ms FSU-on  (r4) | 153.9 | **0** | 153.9 | 52 |

## Findings
1. **Independent duplex is NOT reliably symmetric — it intermittently STALLS the uplink.** 7/8 arms
   ran downlink-only (~152 KB/s) with uplink = 0; the sink's uplink blast wedged (`UP sent=64`, its
   in-flight buffers never completing because the central's downlink blast consumes the airtime /
   credit-return). **1/8** recovered a balanced split. **Same config (25 ms FSU-on) produced both a
   full stall (r1) and a balanced run (r4)** → the stall is **intermittent**, not config-deterministic.
2. **The "≤0.1% symmetry" is an ECHO-RIG ARTIFACT.** With the directions decoupled, symmetry does not
   hold; the echo rig could not show this because its reverse stream is the forward stream echoed back.
   When independent duplex *does* run both ways (r4) it is ~symmetric (imbalance ~1.5%) **but at
   ~77 each (~155 aggregate), not 90+90 = 180.**
3. **Duplex-FSU delta is unmeasurable in this run.** Aggregate is ~150–155 whether FSU is on or off
   (dominated by downlink-only + the stall), so no FSU-duplex signal — consistent with the
   head-to-head "SDC+FSU duplex +14% did not reproduce" and with "duplex already saturates the air."
4. Matches prior evidence: `coc-duplex-artifact-20260814` ("symmetric full-rate CoC-duplex is
   unreachable on this stack without fixing the credit tax") and the flagged intermittent stall.

## Caveats
n=8, single session, one board pair, one RF setting. The uplink stall is the credit / head-of-line
tax (the central's downlink queue starves uplink credit-return), a **known stack limitation**, not a
firmware bug per se — but this build did not defeat it. The ABBA is contaminated by the stall, so no
FSU delta can be extracted.

## Next steps
- **Uplink-fairness mitigation re-run:** shorten the central's downlink queue depth / tune the
  credit watermark (the app has knobs) and re-run to test whether *balanced* independent duplex is
  reliably reachable — or confirm it is not (which would itself be the honest result).
- **More ABBA reps** to quantify the stall rate (here 7/8 stalled, 1/8 balanced).
- Absolute KB/s are RF-day specific; the transferable results are (a) the intermittent uplink stall
  and (b) that independent-stream symmetry ≠ echo-rig symmetry.
