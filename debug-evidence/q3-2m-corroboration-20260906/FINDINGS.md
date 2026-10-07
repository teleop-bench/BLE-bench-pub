# 2M/52µs on-air FSU — corroboration campaign (2026-09-06)

**Goal:** raise confidence in the 2M FSU on-air tIFS measurement (150→52µs) from the single
`q3-2m-smoke-20260813` (n=1, quarantined) to **n≥3 reset-isolated reps**, and get the analyzer to
emit a real per-rep verdict instead of a raw-record read.

**Result: corroborated.** Three independent reset-isolated reps **all** physically show the on-air
tIFS reduction **150.50 → 52.50 µs**, a step of **1568 t = 98 µs (150→52)** with `|d|=0` from the
registered step, cross-validated against the peripheral's on-chip TIFS bins at `|d|=0.00 µs`. Still
**QUARANTINED** (not ACCEPTED) for two *documented equipment/provenance* reasons below — neither is a
physics failure.

## Per-rep (production analyzer, unrelaxed)
| rep | records (ring_full_drops) | PRE gap_proxy | POST gap_proxy | step | class | on-air vs on-chip xval | phase-retention (pre) | production stop |
|---|---|---|---|---|---|---|---|---|
| rep1 | 1558 (0) | 2791 t (150.50µs) | 1223 t (52.50µs) | 1568 t | AGREEMENT `|d|=0` | 98µs vs 98µs `|d|=0` | C 92.9% / P 92.9% | phase-retention < 0.95 |
| rep2 | 1515 (0) | 2791 t (150.50µs) | 1223 t (52.50µs) | 1568 t | AGREEMENT `|d|=0` | 98µs vs 98µs `|d|=0` | **C 97.4% / P 97.4% (PASS)** | idealized baseline only |
| rep3 | 1411 (0) | 2791 t (150.50µs) | 1223 t (52.50µs) | 1568 t | AGREEMENT `|d|=0` | 98µs vs 98µs `|d|=0` | C 94.6% / P 92.2% | phase-retention < 0.95 |

The step (the FSU physics) is **self-calibrating** (PRE−POST within one capture) and passes on all three,
independent of both quarantine reasons. The 150.50/52.50µs (not idealized 150.0/52.0) is this rig's
**real tIFS offset** — the exact 150.50µs the Q2 calibration established (`observer-q2-symctl1/2`).

## The two quarantine reasons (both equipment/provenance, NOT physics)
1. **Provenance-bound 2M `--calib` absent.** Without it the baseline gate compares the measured
   2791 t against the *idealized* `FROZEN_BASELINE_2M = 2783` (150.0µs) at ±2 t; the rig's true
   baseline is 2791 t (150.50µs = Q2-established 150.50µs at the 2M offset 383). **rep2 clears the 95%
   phase-retention and stops ONLY here** — with a valid 2M calib (baseline 2791) it is a fully-clean
   unrelaxed `METRICS-OK` cell (verified by relaxing only the baseline tolerance, retention left at
   0.95). Building a *valid* calib needs 2M FSU-off control cells + `combine_calib` (the anti-forgery
   `validate_calib` rejects a naked json) — that is the ACCEPT-promotion path, not corroboration.
2. **Phase-retention < 0.95 on rep1/rep3** (92.9% / 94.6% pre-plateau). This is the **loss-limited
   single-antenna nRF52 observer** — the frozen gate's own note says *"if the smoke misses it →
   instrument incompleteness + redesign, NOT retune."* We honored that: the threshold was **not**
   lowered for any production result. rep2 shows the observer *can* clear 95% on a good rep; a
   lower-loss / better-positioned observer (or a pro analyzer) would clear it repeatably.

## What made the analyzer 2M-capable (ported here; §9.3 of the acceptance protocol)
The 1M-frozen pipeline falsely rejected the (real) 2M capture at four gates; each was ported so the
2M contract is evaluated instead of bypassed:
- **`observer/CMakeLists.txt`**: forward `-DAIRTIME_MIN_TICKS=256` as a compile def (2M empties ~320 t
  were dropped by the 500 t floor → ring/gap-chain broken). *Also required raising the connection
  interval 7.5→20 ms so 30 s of retained-2M-empties fits the 2048-record ring — `ring_full_drops` went
  1967→0; tIFS is interval-independent (app comment + unchanged result).*
- **config-binding `observer PHY==1M`** → `EXPECT_PHY` global (1 default; 2M override sets 2).
- **request-integrity** `registered 52→52` → `52→150` + `phys 0x1→0x2` (matches `fsu-f52.conf` /
  firmware `Q3FSU-REQ min=52 max=150 phys=0x2`, mirroring the 1M arm's register-equals-request rule).
- **on-chip cross-val `phy != 1`** (both call sites) → honor `EXPECT_PHY`.

These are backward-compatible (1M behavior unchanged; `EXPECT_PHY` defaults to 1).

## Provenance
- Firmware rebuilt from the `q3-2m-smoke-20260813` snapshot on zephyr `fsu-m0` (v4.4.1-16-g8f44a3d7a3f4);
  observer `-DQ2=1 -DAIRTIME_MIN_TICKS=256`; central `fsu-f52.conf` (52/150, PHY_2M, EVENT_IFS_LOW_LAT=52,
  20 ms interval); periph `fsu.conf` (TIFS_CAPTURE_BENCH). Sources archived in `firmware-src/`.
- Boards: obs 1050347760, central 1057794857, periph 1057719509 (same rig as the 08-13 smoke).
- Each rep = a separate runner invocation (resets both endpoints + observer, fresh AA) → reset-isolated.
- Per-rep `manifest.json` + `obs.txt`/`central.txt`/`periph.txt` retained; every rep `--smoke` → the
  cell verdict is QUARANTINED by construction.

## Calib slice — DONE: rep2 is a clean unrelaxed accepted-quality cell (2026-09-06)
Built the 2M calibration path — **`analyze_q2_2m.py`** (rebinds `EXPECT_PHY=2` + `PREAMBLE_AA_OFF=383`
+ `TIFS_EXPECT=2783`, delegates to `analyze_q2.analyze`) + **`combine_calib_2m.py`** + wired the
runner's plain-Q2 path to the 2M analyzer — and froze a provenance-bound calib:
- **Symmetric controls achieved by balancing endpoint TX** (the ≤6 dB gate). The snapshot firmware was
  asymmetric — central `TX_PWR_PLUS_8` (+8 dBm), periph `TX_PWR_MINUS_20` (−20 dBm) = 28 dB baked in;
  plus ~7 dB geometry = the ~41→35 dB observed sep. After the boards were repositioned equidistant
  (retention → 99%) **and** rebuilt **central +1 / periph +8 dBm** (offsetting central's ~7 dB path
  advantage), the sep dropped to **4 dB**. 2 controls (`calib-controls/ctrl{1,2}`) ACCEPT: `gap_proxy
  2790.5 / 2791`, retention 99.4% / 99.3%, sep 4 dB.
- **`combine_calib_2m` → `gap-proxy-calibration-2m.json` = ESTABLISHED**, `frozen_gap_proxy_dev = 2791 t`
  (150.50 µs), spread 0.5 t — the rig's Q2-established baseline, provenance-bound (both controls
  re-analyzed ACCEPT + manifest/homogeneity gates).
- **rep2 with `--calib` → unrelaxed `METRICS-OK`:** production contract (`baseline_tol_ticks=2`,
  `retention_min=0.95`, `phase_retention_min=0.95`), `[frozen baseline 2791 +/-2 OK]`, whole-cell
  97.3/96.9%, phase-ret 0.974/0.959, PRE 2791 → POST 1223, **step 1568 `|d|=0` AGREEMENT**, on-air==on-chip
  `|d|=0`. Every gate passes with no relaxation — it stays "QUARANTINED" only by the `--smoke` flag /
  absence of the formal ACCEPT collection run, not by any failing gate. rep1/rep3 still miss the 95%
  phase-retention (loss-limited observer) — the calib doesn't change that.

Two real analyzer bugs fixed along the way (both silent-wrong-data traps): (a) `q3_2m_analyze`
defaulted `calib_frozen=2783`, which is non-None and so **shadowed `--calib`** (the calib never loaded)
— now defaults to None unless no `--calib`; (b) the CLI only parsed flags **before** the positionals, so
a **trailing `--calib` (exactly how the runner passes it) silently no-op'd** — now parsed in any
position.

## Net
`q3-2m-smoke-20260813` (n=1, raw-record read) → **n=3, clean-ring captures, analyzer verdict per rep,
step cross-validated `|d|=0` on every rep.** The 2M/52 on-air reduction is now corroborated. Formal
ACCEPT still requires (a) a provenance-bound 2M calib (2M FSU-off controls + `combine_calib`), (b) a
lower-loss observer to clear 95% phase-retention repeatably, and (c) the ABBA confirmation
(`combine_abba` ported to the 2M f150/f52 sequence). Those are the remaining pieces of the promotion.
