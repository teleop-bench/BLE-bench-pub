# Q3-2M §9.5 pilot — saturated same-session foundation VALIDATED (2026-08-13)

Pilot for the accepted campaign: does the §9.1 merged central (saturating blast + observer
binding) give a stable, saturated 2M/50 ms link whose tIFS gap chain stays clean? A
saturated capture at FSU-OFF (f150, no trigger). Result: all §9.5 checks PASS.

## PASS
- **Stability**: 2M / 50 ms / 2-channel-pinned {10,11} + saturating blast — ZERO
  disconnects over the whole run (the periph `GAP_AUTO_UPDATE=n` fix holds at 50 ms).
- **Saturation**: MTU exchanged to 247 (else 244 B writes fail EMSGSIZE — the fix), blast
  running; observer len distribution ~1041 DATA(251 B) : 1004 empty (near 1:1 = full
  events); on-chip `tx=34130` (vs ~300 unsaturated).
- **Gap chain clean under REAL saturation**: dominant plateau gap 174 → gap_proxy 2784 t
  ≈ 2783 = 150 µs. The interleaved 251 B DATA packets do NOT contaminate the per-tIFS
  chain — the single biggest §9 unknown, resolved positively.

## The one engineering item for the accepted cell (§4 capacity)
- `ring_full_drops=15508`: ~585 pkt/s on ch10 ⇒ the 2048 ring fills in ~3.5 s.
- Fix: F-trigger at ~1.5 s dwell + a ~3 s capture window fits BOTH plateaus (~880 baseline
  + ~1160 post-FSU < 2048), OR enlarge `RING_N`. Measure-then-size, per §4.

## Firmware (merged, §9.1)
- central-2m: 2M PHY, FSU phys=2M/f52, map pinned {10,11}, **ATT MTU exchange**, discover
  bulk-sink char, 244 B blast thread, 50 ms connect. periph-2m: 2M, FSU-accept, IFS-clamp
  52, TIFS_CAPTURE_BENCH, GAP_AUTO_UPDATE=n, bulk-sink char. Both: DLE/MTU/deep-buf
  (`sat.conf`). observer: PHY2M, AIRTIME_MIN=256, AIRTIME_MAX=34000 (accepts 251 B DATA).

## Next (accepted cell): size ring/window (above) + PORT the 3 gates (§9.3: f52 arm,
assert_fsu_config_2m, provenance 2M --calib) + ≥2 reset-isolated mid-step cells + the §5.5
falsifiability controls + the §9.2 paired goodput ±FSU.

## UPDATE: BOTH saturated plateaus captured (steady arm) — 2026-08-13

The mid-step (150→52 in one connection) does NOT fit the RAM-limited ring at 50 ms
saturated (RING_N>2560 overflows the nRF52832's 64 KB; the runner needs ≥3 s baseline;
~585 pkt/s fills 2048 in ~3.5 s — both plateaus can't coexist). So the accepted cell uses
the **STEADY arm**: separate SATURATED single-plateau captures for f150 and f52.

- **Saturated f150** (this pilot): gap 174 → gap_proxy 2784 t ≈ 2783 = 150 µs.
- **Saturated f52** (`f52-saturated/`, auto-FSU at connect, `spacing=52 phys=2M`,
  887 DATA:868 empty, 0 disconnects): **1677 records at gap 76 → 1216 t ≈ 1215 = 52 µs.**
- **Step = 1568 t = 150→52 µs, in the SATURATED goodput regime** — addresses the
  reviewer's same-session-saturation concern (the first smoke was low-rate).

QUARANTINED still: single captures each (no ABBA drift-cancel), gates not ported (f52 arm,
assert_fsu_config_2m, provenance --calib), no §5.5 clamp-removed control, no §9.2 goodput.
Firmware added: central `CONFIG_APP_AUTO_FSU` (steady arm: request FSU at connect).
