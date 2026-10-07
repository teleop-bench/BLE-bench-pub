# GATT echo duplex FSU — matched arms, held FSU, post-onset window (2026-10-03)

**Status: RESOLVED (for GATT echo duplex at 7.5 and 25 ms).** Replaces the open question left by the
08-24 GATT duplex ABBA (confounded FSU-off arm) and the quarantined 09-06 CoC duplex "null" (FSU
revert artifact).

## Result (aggregate = central `exkBps` echo received + peripheral `rxkBps` blast received)
| interval | FSU off (n=4) | FSU on (n=4) | FSU gain, Welch 95% CI | separation |
|---|---|---|---|---|
| 7.5 ms | 187.0 KB/s (186–188) | 187.5 KB/s (187–188) | **+0.3% ± 1.0%** | null (ranges overlap) |
| 25 ms | 184.0 KB/s (182–186) | 201.5 KB/s (198–204) | **+9.5% ± 2.1%** | strict non-overlap |

16/16 reps accepted, 0 rejected. Duplex FSU gives no gain at 7.5 ms and a real gain at 25 ms. The
08-24 positive (+11.9% at 25 ms) is broadly confirmed in direction and size; its confounded off-arm did
not manufacture it. The mechanism for the 7.5 ms null was not measured.

## Design (every known duplex/FSU trap designed out)
- **Matched arms:** central `apps/nrf54l15/z54-lat-central` with `tput-open.conf;fsu-open.conf` +
  `hh-fsu52.conf` (on) or `hh-fsu150.conf` (off), at `CONFIG_APP_CONN_INT_UNITS=6` / `20`.
  `check_matched_pair.py`: both pairs MATCHED (differ only in `APP_FSU_MIN/MAX_US`), see
  `design-gate.txt`. Both arms run the FSU procedure.
- **Shared FSU-capable echo peripheral, auto-update off:** `apps/nrf54l15/z54-lat-periph` with
  `tput-echo-open.conf;fsu-open.conf` + `-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`; resolved config
  confirms auto-update off, `CONN_INTERVAL_LOW_LATENCY=y`, `EVENT_IFS_LOW_LAT_US=52` (`configs/p-echo.config`).
- **Per rep:** capture from boot (ports opened, then both boards reset); FSU-on requires
  `FSU: updated ... spacing=52`; FSU-off requires `FSU: request [150..150] rc=0` and no 52 µs anywhere
  (a 150 request on a 150 link is a no-op, so no "updated" event); interval must equal the requested
  one throughout; no disconnects; FSU held (`verify_run.fsu_held`) on both directions.
- **Window:** from FSU completion (FSU-off: the request) + 2 s to the end of a 45 s capture; medians.
- **Order:** ABBA ×2 (off,on,on,off,off,on,on,off) per interval. Smoke (one rep per arm) in
  `results-smoke.jsonl` / `caps-smoke/`, run first; the first full attempt was stopped after the smoke
  check exposed the FSU-off timing bug, and its captures were discarded before the rerun.

**Rig:** 2× nRF54L15-DK (central 1057719509, peripheral 1057794857), close range, open Zephyr
`v4.4.2-16-gfee9fbc`, 2M PHY.

## Scope / caveats
- **GATT echo duplex only.** The aggregate is meaningful; the per-direction split is coupled by the echo
  (both directions read identical here) and is not a symmetry result.
- CoC duplex with independent traffic in each direction is not covered (the reconnect wedge blocks it).
- One session, n=4 per arm per interval, two intervals.

## Files
`harness.py` (as run), `results.jsonl`, `caps/` (raw logs), `firmware/` (hexes + SHA256SUMS),
`configs/` (resolved `.config` for every image), `design-gate.txt`. Local paths scrubbed with
`tools/scrub-paths.py`.
