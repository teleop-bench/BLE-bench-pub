# 2M/52µs on-air FSU — FORMAL ACCEPTANCE (2026-09-06)

The open-Zephyr M0 FSU **physically reduces on-air tIFS from 150 µs to 52 µs at the 2M PHY**, now
**formally accepted** through the same preregistered protocol the 1M (150→100) cell passed
(`fsu-q3a-20260812`): a PRIMARY within-connection experiment + an ABBA drift-cancelling CONFIRMATION,
all cells non-`--smoke`, calib-bound, clean-tree, provenance-verified.

## PRIMARY — 3 accepted reset-isolated mid-step cells (`primary/mid{1,2,3}`)
Central requests FSU [52..150] mid-capture; the on-air step is measured against the ESTABLISHED 2M
calib (2791 t), cross-validated against the peripheral on-chip TIFS bins.

| cell | on-air step | class | on-air vs on-chip xval | verdict |
|---|---|---|---|---|
| mid1 | 1568.0 t | AGREEMENT `|d|=0.0` | 98.00 vs 98 µs `|d|=0.00` | **ACCEPT** |
| mid2 | 1569.0 t | AGREEMENT `|d|=1.0` | 98.06 vs 98 µs `|d|=0.06` | **ACCEPT** |
| mid3 | 1569.0 t | AGREEMENT `|d|=1.0` | 98.06 vs 98 µs `|d|=0.06` | **ACCEPT** |

Every production gate passes with no relaxation: `baseline_tol=2`, `retention_min=0.95`,
`phase_retention_min=0.95`, registered arm `f52`, verified observer image + endpoint provenance,
clean pre-run tree.

## CONFIRMATION — 4-cell ABBA, drift-cancelled (`confirmation/`)
Steady single-plateau cells in the frozen f150/f52/f52/f150 order, 4 distinct reset-isolated
connections, param-update event-gated (interval → 50 ms) so the FSU persists:

| seq | arm | plateau median | AA |
|---|---|---|---|
| 0 | f150 | 2791 t (150.50 µs) | 0xcd028f1a |
| 1 | f52  | 1223 t (52.50 µs)  | 0x48a6a072 |
| 2 | f52  | 1223 t (52.50 µs)  | 0x470c2678 |
| 3 | f150 | 2790.5 t (150.47 µs)| 0x58682078 |

**Drift-cancelled step = ((A1−B1)+(A2−B2))/2 = (1568 + 1567.5)/2 = 1567.75 t (~97.98 µs),
`|d|=0.25` ≤ 4 → `ABBA-CONFIRMED`** (`confirmation/abba-confirmation.json`). All combiner gates pass:
manifest provenance, firmware re-verify (2M asserter), re-analysis source-of-truth, campaign binding,
homogeneity, distinct AAs, current-lineage.

## What was built to reach ACCEPT (2M port of the 1M-frozen machinery)
- **`assert_fsu_config_2m.py`** — 2M config gate (requires PHY_2M + explicit-2M-drive USER_PHY_UPDATE;
  arms f52/f150/periph). Drops the `--smoke` bypass.
- **`analyze_q2_2m.py` / `combine_calib_2m.py`** — the provenance-bound 2M calib path (→ 2791 t, from
  `q3-2m-corroboration-20260906`).
- **`analyze_q3_2m.py`** — 2M analyzer + `_apply_2m_overrides` (EXPECT_PHY, gap-proxy offset 383,
  step 1568, ABBA seq f150/f52, request-integrity 52→150 phys 0x2).
- **`combine_abba_2m.py`** — 2M ABBA combiner (f150/f52, step 1568).
- **`q2_run_2m.py`** — registers f52 (mid-step) + f150/f52 (steady); in-process 2M overrides;
  steady on-chip bin derived from the request; several stale post-reorg asserter/protocol paths fixed.

## Rig notes (reproducibility)
- **Two peripheral builds, one per experiment** (both assert-pass; auto-update is not gated):
  - **mid-step** uses `periph fsu.conf` (auto-update **off**) — a pinned 20 ms interval gives a clean
    baseline plateau; the auto-update variant corrupts the mid-step baseline (interval change mid-window).
  - **steady/ABBA** uses `periph fsu-au.conf` (auto-update **on** + pref 50 ms + 5 s timeout) — the runner
    event-gates FSU on the ~5 s conn-param update so the negotiated frame space persists.
- Central: `fsu-f52` (mid-step + ABBA B-cells) and `fsu-f150` (ABBA A-cells) — distinct images
  (combiner requires it). TX balanced +1/+8 dBm (rig-specific; see the corroboration dir).
- Calib: `gap-proxy-calibration-2m.json` (ESTABLISHED, 2791 t) — the same artifact all 7 cells bound to.
- Firmware sources in `firmware-src/`. Cells carry per-cell `manifest.json` + `firmware/` hashes.

## Remaining honesty
This is ACCEPTED *by our protocol via the nRF52 observer*, not independently qualified by a professional
analyzer (Ellisys/Frontline) — the observer is loss-limited and that equipment gap is unchanged. The
physics + the acceptance rigor are what's established here.
