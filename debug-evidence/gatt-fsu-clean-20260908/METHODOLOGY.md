# Clean GATT one-way FSU vs interval — confound-free re-run (2026-09-08)

**Why this dir exists.** The GATT FSU deltas in the earlier `fsu-interval-sweep` / `sdc-fsu-interval-sweep`
were **confounded**: the FSU-off arm dropped the entire FSU feature package (36 differing resolved-config
symbols, incl. `BT_BUF_EVT_RX_SIZE` 255→68), not just the requested frame space — so the on/off delta
wasn't attributable to FSU alone. A second issue: the SDC-GATT FSU *sink* lacked the responder floor, and
throughput was a whole-run median that **blended pre/post-FSU** (SDC negotiates FSU ~16 s into a 30 s run).
This re-run fixes all three and is enforced by gates.

## What changed
- **Matched off-arm.** Both arms share the full feature package + FSU-capable sink; they differ ONLY in
  the requested frame space (`APP_FSU_MIN/MAX_US` = 52 on / 150 off). Overlays: `hh-fsu52.conf` (on) vs
  `hh-fsu150.conf` (open off) / `hh-sdc-fsu.conf` vs `hh-sdc-nofsu.conf` (SDC).
- **FSU-capable sinks**, shared by both arms (`fsu-open.conf` / `sdc-fsu.conf`), auto-update-OFF.
- **Post-FSU window.** Throughput = median of the late window, clipped to start strictly after the FSU
  onset (parsed from the cen-log `spacing=` timestamp) — never straddling the transition.
- **Student-t 95% CIs** (n=8), counterbalanced order.

## Gates (ENFORCED — nonzero exit, not advisory)
- `tools/check_matched_pair.py` on each on/off pair's *resolved* `.config` → **10/10 MATCHED** (differ only
  in `APP_FSU_MIN/MAX_US`). Configs archived in `configs/`; the verification over those archived configs is
  persisted in **`gate-verification.log`** (10/10 MATCHED + both sinks capable — PASS).
- Sinks verified auto-update-OFF + FSU-floor-capable (a non-capable sink is a hard fail, not a warning).
- Enforcement: `build_gattclean.sh` **exits 1** if any pair is confounded or a sink is non-capable;
  `harness.py` runs `preflight_gate()` on the **live build configs beside the flashed hexes** and
  `sys.exit(1)`s before flashing anything; a flash failure fails the cell. (This campaign was gated at build
  time; `gate-verification.log` re-confirms the archived configs, and the harness preflight enforces the same
  for re-runs.)
- `verify_run` per capture (held / dead-link / arm-aware spacing) → **160/160 reps accepted**, 0 rejects.

## Results (post-FSU window, Student-t, n=8)
| interval | OPEN off→on / FSU | SDC off→on / FSU |
|---|---|---|
| 7.5 ms | 158→189 / **+19.8% ±0.2** | 126→157 / **+24.6% ±0.6** |
| 15 ms | 157→189 / +20.2% ±0.3 | 142→173 / +21.9% ±0.2 |
| 25 ms | 161→188 / +16.9% ±0.7 | 160→179 / +12.2% ±0.7 |
| 37.5 ms | 163→193 / +18.7% ±0.7 | 163→182 / +11.7% ±1.2 |
| 50 ms | 165→192 / +16.6% ±1.0 | 161→183 / +13.2% ±1.4 |

These supersede the earlier confounded/window-mixed GATT numbers. The re-run changed three things at once
(matched off-arm, FSU-capable sink, post-FSU window), so the campaign-to-campaign difference is **not**
decomposable to a single cause — the earlier GATT deltas are simply not trustworthy. Separately, re-windowing
the SAME old SDC-GATT logs (single variable) shows the old whole-run "+7% at 50 ms" was a window artifact
(→ +14.3%; see `../postfsu-reanalysis-20260908/`).

## Reproduce
Archived in this dir: `build_gattclean.sh` (builds all 22 arms + archives resolved `.config` + runs the
matched-pair design gate), `conductor_gattclean.sh` (smoke→full driver), `harness.py <reps>` (the gated
sweep). Canonical step-by-step is [`REPRODUCE.md` → GATT one-way]. Absolute KB/s are RF-day-specific
(single session); the deltas and shape transfer. Firmware SHAs in `FIRMWARE-SHA256.txt`; the 22 resolved
`.config` in `configs/`. (Scripts carry the original absolute build/device paths — adjust for your rig.)
