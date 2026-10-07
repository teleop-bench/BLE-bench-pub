# Reset-recovery endurance (n=100) — 2026-09-09

Raises the forced-reset reconnect count from **50** (`endurance-1m` §14.7, the sub-ms 1M echo rig) to
**100**, on the `coclat2` two-channel rig — the same dedicated-lane architecture the safety doc recommends,
so its reconnect robustness is directly relevant. Each reset must fully re-establish **both** L2CAP
channels (a fresh "CTRL chan up" in the central log); a reset that doesn't within 20 s is a WEDGE.

**Rig/firmware:** identical `coclat2` builds as `coc-dedicated-lane-retest-20260909` (SHA + configs here).
100 alternating peripheral/central resets, 12 s gaps, recovery timed from `nrfutil reset` to the new
CTRL-up. `harness.py [N] [gap_s]` reproduces; `caps/` holds the spanning central/peripheral capture.

## Result
- **100/100 recovered first-try** (50 peripheral resets + 50 central resets), **0 wedges**
- **Recovery time: min 4.5 s · mean 4.54 s · max 4.8 s** — identical for peer and central resets
- Both L2CAP channels re-establish every time; the two-channel architecture reconnects as robustly as it
  isolates.

**Caveat (unchanged):** a *reset* is warm; a cold **power-cycle** is a distinct fault — detection/reconnect
recover, but the high-rate bulk stream can stall until re-established (see the doc's honest-gaps note).
