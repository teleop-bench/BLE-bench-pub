> **RETRACTION (2026-08-25): the "RF-suppressed / CoC is RF-fragile" conclusion below (the
> §"one exception" and §"This IS the RF-robustness finding" sections) is WITHDRAWN.** A
> file-level audit found the "170→131 same-binary drop" premise is false: `coc-f52.hex` has its
> interval **hardcoded to 50 ms** (`central-main.c:228`) and could never produce ~170 (that was a
> separate 480 B / 12.5–15 ms build); its own 08-13 record is ~150 FSU-on / ~131 FSU-off at 50 ms.
> So the re-flash's ~131 is this binary's normal 50 ms number, not a degraded 170 — no RF needed.
> The ~130 ceiling is a known non-RF open-host limit (documented 08-12 for GATT *and* CoC). The
> sink credit path is byte-identical to the original (sha256). See `coc-technical-overview.md` §4
> (rewritten) — it supersedes everything below. Left in place unedited as a record of the wrong turn.

# Full throughput matrix re-run (2026-08-24/25) — findings

## Result: clean 84-cell matrix at realistic ~2 ft (matrix-2ft-clean.csv)
5 of 6 transport×stack rows are clean, one arrangement, FSU converting, realistic absolutes
(not the touching-adjacent silicon ceiling). Doc Table 1's 5 clean rows refreshed to these:

| row | No-FSU | FSU | gain |
|---|---|---|---|
| GATT one-way open | 153 @7.5 | 178 @7.5 | +16.3% |
| GATT one-way SDC  | 144 @25  | 165 @25  | +14.6% |
| GATT duplex open  | 174 @25  | 190 @25  | +9.2%  |
| GATT duplex SDC   | 158 @25  | 189 @25  | +19.6% |
| CoC one-way SDC   | 136 @15  | 155 @15  | +14.2% |

## The one exception: CoC one-way OPEN — RF-suppressed, NOT reproducible in this room
Open CoC measured ~130 KB/s (FSU-noise) here. Extensive diagnosis ruled out reconstruction,
SDU size, central version, and the ISR instrumentation. The DECISIVE test: the **exact
committed original hex** (`../coc-open-fsu-20260813/coc-f52.hex`, which measured ~170 on
2026-08-13) **now measures ~131, bouncing 121-149**, same sink. ⇒ the 170→131 drop is the
**RF ENVIRONMENT** (current 2.4 GHz worse for CoC), not code. Doc keeps the original
well-provenanced ~170/+23% for that row (the real capability, cleaner-RF day).

## This IS the RF-robustness finding (same firmware, controlled across environments)
- GATT: stable ~178 (Aug-13 and now)
- open CoC: 170 -> 131 + bouncing (Aug-13 -> now)
Same binaries. GATT holds under worse RF; CoC collapses. Strong (n=2-environment) evidence
that **L2CAP CoC is more RF-fragile than GATT** — see gatt-vs-coc-teleop notes + the
degraded-RF campaign objective. Needs the controlled attenuation sweep to become a curve.

## Provenance / caveats
- ~2 ft is the realistic operating point; touching-adjacent gave silicon-ceiling absolutes
  (GATT 197 / duplex 214, ~2M max) — not deployable, discarded.
- CoC-open row is a cleaner-RF day than the other 5 rows (2 ft, this run) — a deliberate
  choice: the current room RF-suppresses CoC-open, proven by the original hex.
- fsu-m0 ISR event-fill diagnostic gated behind CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG (default n)
  for benchmark hygiene (wasn't the CoC-130 cause, but shouldn't ship on in throughput builds).
