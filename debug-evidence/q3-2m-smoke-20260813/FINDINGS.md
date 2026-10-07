# Q3-2M SMOKE — open FSU physically reduces tIFS at 2M (QUARANTINED) — 2026-08-13

**Purpose:** resolve the goodput-run ambiguity — at 2M the host reported `spacing=52 us`
but the throughput rig saw no effect; only an independent on-air instrument can say
whether tIFS physically dropped at 2M.

**Status: SMOKE, QUARANTINED. Not an accepted cell.** Per the Q3-2M-ACCEPTANCE-PROTOCOL
(REV 2), this is a pipeline-bring-up run, not a claim.

## Result (both legs agree)

FSU triggered mid-connection (`Q3FSU-DONE status=0x00 spacing=52 phys=0x2` = 2M):
- **Independent RF observer** (nRF52832, END-capture, 2M, AA/CRCInit/channel bound):
  gap_proxy **2784 t → 1216 t** (gap_us 174 → 76; 406 + 576 clean records), i.e.
  tIFS **150.06 → 52.06 µs**. Matches the frozen 2M calibration exactly (2783 / 1215;
  offset 383) and the registered step **1568 t** (= 98·16).
- **On-chip controller** (periph `TIFS_CAPTURE_BENCH`, phy=2): `tifs=150 med=145` →
  `tifs=52 med=47`, a 98 µs step.
- **Cross-val**: observer step/16 = 98 µs vs on-chip 98 µs → **|Δ| = 0 µs** (≤ 8 µs).

**Interpretation:** the open Zephyr M0 FSU *physically* reduces on-air tIFS 150→52 µs at
2M. The `spacing=52` host report corresponds to a real on-air reduction — so the earlier
2M goodput non-effect is **not** an FSU-application failure; it is a downstream
event-fill/occupancy matter.

## Why this is QUARANTINED (do NOT promote)

1. **Runner verdict = QUARANTINED** (`q3-fsu-config-invalid`): the 1M-frozen
   `assert_fsu_config.py` + the `--q3-arm f100`/f150-only registration reject the 2M/f52
   config; I bypassed the asserter under `--smoke`. An accepted 2M cell needs a proper 2M
   asserter contract + an `f52` arm registration.
2. **Low-rate link, NOT the saturated throughput session.** `q2-central` connects but does
   not blast (`Q2EVT`/empty-PDU rate, not ~29 pairs/event). So this proves **2M FSU
   capability**, NOT that 52 µs was physically active in the earlier goodput blast run.
   Protocol §2.5.3 (same-session + saturated) is unmet.
3. **Single smoke.** Establishment needs ≥ 2 reset-isolated accepted cells (+ the §5.5
   falsifiability controls: f150 → 2783; a clamp-removed config → ~2783).
4. **No frozen 2M `--calib`.** The analyzer used its built-in 2783 default (smoke bypass),
   not a provenance-bound calibration artifact; the analyzer exited INCOMPLETE on the
   config gate before computing a plateau median, so the numbers above are read from the
   raw observer/on-chip records, not an accepted analyzer verdict.

## What it takes to make it an ACCEPTED result (next campaign)

- A 2M `assert_fsu_config` contract + `f52` arm in the runner (the last 1M-frozen gates).
- Make the observed session the **saturated** one (add the blast to `q2-central`, or add
  the pin-map + `Q2CONN` + FSU-trigger to the blast central) so the tIFS measured is the
  goodput-run's.
- ≥ 2 reset-isolated cells + the f150 and clamp-removed falsifiability controls +
  provenance-bound `--calib` + full manifest.

## Firmware / config (this smoke)

- central-2m: 2M PHY (explicit `phy_update->2M`), FSU `phys=2M_MASK`, f52 (MIN 52), map
  pinned {10,11}. periph-2m: 2M, FSU-accept, `EVENT_IFS_LOW_LAT_US=52`,
  `TIFS_CAPTURE_BENCH=y`, `GAP_AUTO_UPDATE_CONN_PARAMS=n` (the fix for the 0x08 drop that
  killed smoke-1 — the ~5 s auto conn-param update). observer: `-DPHY2M=1`,
  `AIRTIME_MIN_TICKS=256` (a 2M empty PDU is ~20 µs / 320 t; the 1M 500 t floor would drop
  empties and break the per-tIFS gap chain — confirmed: empties captured at `air_us=20`).
- Harness: `q2_run_2m.py` (phy=2 CFG, `analyze_q3_2m`, on-chip `expect_phy=2`, smoke
  bypasses of the 1M calib + config gates), `analyze_q3_2m.py` (offset 383, step 1568,
  bins {150,52}).
