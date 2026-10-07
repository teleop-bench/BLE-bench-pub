# q2-central-2m / q2-periph-2m — the 2M FSU on-air endpoints (formally accepted)

First-class, buildable sources for the **2M/52 µs on-air FSU** endpoints whose acceptance is banked in
[`debug-evidence/q3-2m-accept-20260906/`](../../../debug-evidence/q3-2m-accept-20260906/) (3 primary
mid-step AGREEMENT cells + 4-cell ABBA-CONFIRMED). These were reconstructed to first-class apps so the
accepted firmware rebuilds turnkey (previously they lived only in `/tmp` + a flat `firmware-src/` archive).

Driven by the runner + analyzers in [`../q2-central/`](../q2-central/) (`q2_run_2m.py`,
`analyze_q3_2m.py`, `combine_calib_2m.py`, `combine_abba_2m.py`) and gated by
[`zephyr-patches/fsu-m0-series/assert_fsu_config_2m.py`](../../../zephyr-patches/fsu-m0-series/assert_fsu_config_2m.py).

## Central (`q2-central-2m`)
2M port of `q2-central`: drives the link to 2M explicitly (`bt_conn_le_phy_update(2M)`;
`prj.conf` sets `BT_CTLR_PHY_2M=y` + `BT_USER_PHY_UPDATE=y`), requests FSU on the 2M PHY mask, and pins a
20 ms interval (ring-fit for the 30 s observer capture). Two arms via overlay:
- `-DEXTRA_CONF_FILE=fsu-f52.conf` — the reduced arm (request 52→150 µs). Mid-step + ABBA B-cells.
- `-DEXTRA_CONF_FILE=fsu-f150.conf` — the no-request 150 µs control. ABBA A-cells.

## Peripheral (`q2-periph-2m`)
Responder + on-chip TIFS bench (`BT_CTLR_PHY_2M=y`). **Two overlays — one per experiment** (both pass
`assert_fsu_config_2m --arm periph`):
- `-DEXTRA_CONF_FILE=fsu.conf` — auto-update **off**. Use for **mid-step** (pinned interval → clean
  baseline plateau; the auto-update variant corrupts the mid-step baseline via a mid-window interval change).
- `-DEXTRA_CONF_FILE=fsu-au.conf` — auto-update **on** (pref 50 ms, 5 s timeout). Use for **steady/ABBA**
  (the runner event-gates FSU on the ~5 s conn-param update so the negotiated frame space persists).

## TX power (rig-specific)
`prj.conf` carries the balanced TX (central +1 / periph +8 dBm) that made the symmetric calibration
controls pass on this bench (~7 dB geometry offset). **These are rig-specific** — a different bench needs
different values (balance *received* power at the observer to ≤6 dB); see `docs/LESSONS.md`.

## Build
```
west build -b nrf54l15dk/nrf54l15/cpuapp -d BUILD apps/misc/q2-central-2m -- -DEXTRA_CONF_FILE=fsu-f52.conf
west build -b nrf54l15dk/nrf54l15/cpuapp -d BUILD apps/misc/q2-periph-2m  -- -DEXTRA_CONF_FILE=fsu.conf   # or fsu-au.conf
# observer: west build -b nrf52dk/nrf52832 apps/nrf52/pca10040-radio-observer -- -DQ2=1 -DAIRTIME_MIN_TICKS=256
```
Or let `tools/observer-smoke.sh 2m` detect the boards, build all three images and run `q2_run_2m.py` for
you; see REPRODUCE.md, *On-air FSU observer*, for pass criteria and the accepted-cell (mid-step/ABBA) recipes.
