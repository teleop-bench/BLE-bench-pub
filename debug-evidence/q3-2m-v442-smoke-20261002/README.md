# 2M/52 µs on-air FSU smoke on Zephyr v4.4.2-16 — 2026-10-02

**Status: QUARANTINED (integration smoke, by design).** A corroboration that the accepted 2M on-air
FSU step (`q3-2m-accept-20260906`, built on `v4.4.1-16-g8f44a3d`) also holds on the
`v4.4.2-16-gfee9fbc` tree that REPRODUCE now recommends. Not an acceptance: `--smoke` forces a
non-accepting verdict, n is small, and no calibration controls or ABBA were run.

**Rig:** nRF52832 DK observer (1050347760) + 2× nRF54L15-DK (central 1057794857, peripheral
1057719509), the same role mapping and TX powers (central +1 dBm, peripheral +8 dBm) as the accepted
cells. Images built 2026-10-02 on `fsu-m0-v442` @ `fee9fbc`: observer
`pca10040-radio-observer -DQ2=1 -DAIRTIME_MIN_TICKS=256`; central `q2-central-2m` + `fsu-f52.conf`;
peripheral `q2-periph-2m` + `fsu.conf` (auto-update off). Both endpoint configs pass
`assert_fsu_config_2m.py`.

**Command (per rep):** `apps/misc/q2-central/q2_run_2m.py --mode symmetric
--calib debug-evidence/q3-2m-corroboration-20260906/gap-proxy-calibration-2m.json --q3 --q3-arm f52
--baseline-dwell-s 6.0 --smoke` plus the ports/serials/build dirs (full arguments in each `manifest.json`).

## Results

| rep | verdict | detail |
|---|---|---|
| `rep1-retention-incomplete/` | QUARANTINED, analysis INCOMPLETE | all structural + config-binding gates PASS; FSU completed on both ends; central phase retention 94.8% pre-step < the frozen 95% gate, so the step was not computed. On-chip bins: 150 µs programmed → 145 µs measured (n=312), 52 → 47 µs (n=1187), drop=0. |
| `rep2-metrics-ok/` | QUARANTINED (smoke), **METRICS-OK** | whole-cell retention 99.2% C / 99.1% P; pre gap 2790 t (≈150.44 µs, frozen baseline 2791 ±2 OK) → post 1222 t (≈52.44 µs); **step 1568 t vs 1568 t, \|d\|=0, AGREEMENT**; on-air 98.00 µs vs on-chip 98 µs, \|d\|=0.00 µs. |

The rep-1 near miss is the known retention limit of the single-antenna observer at 2M (LESSONS:
"95% phase-retention is a frozen instrument gate"); the gate was not relaxed.

The analyzer/runner print some legacy 1M wording ("on-air step == 50us", "150-100 delta",
"@1M bin(s)"); the computed 2M values above are correct (see REPRODUCE).

ELF images are not archived (repo policy: `debug-evidence/**/*.elf` is gitignored); the `.hex`,
`.config` and logs are. Local paths in the text files are scrubbed with `tools/scrub-paths.py`;
check recorded hashes with `python3 tools/scrub-paths.py sha256 <file>`.
