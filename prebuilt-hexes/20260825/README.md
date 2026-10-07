# Prebuilt hexes — 2026-08-25 re-test session (nRF54L15-DK, open `fsu-m0` v4.4.1-15-g1772af7)

Turnkey flash-and-verify firmware for the 2026-08-25 findings (matched-config reproduce,
CoC-vs-GATT range, FSU-vs-interval, CoC-duplex, refill lever). `nrf54l15dk/nrf54l15/cpuapp`, 2M
PHY. Verify with `SHA256SUMS`. Every hex is rebuildable from committed source via the recipe in
its row (see also `REPRODUCE.md`). Flash peripheral first, then central; capture with
`debug-evidence/latency-under-load-20260813/capture-tool.py`. **Reset both boards fresh before
each capture** (stale-tag gotcha).

| hex | app + recipe | used for |
|---|---|---|
| `coc-central-480-15ms-fsu-on.hex` | `coc-central` `open-fsu.conf` `-DCONFIG_APP_SDU_SIZE=480 -DCONFIG_APP_CONN_INT_UNITS=12` | Test A (matched config → ~162 @10cm); FSU-on |
| `coc-central-480-15ms-fsu-off.hex` | `coc-central` `open-nofsu.conf` same -D | Test A/B FSU-off control |
| `coc-sink-open-fsu.hex` | `coc-sink` `open-fsu.conf` | CoC sink (seg_recv + FSU responder) for all one-way CoC |
| `coc-central-244-15ms-nofsu.hex` | `coc-central` `open-nofsu.conf` `-DCONFIG_APP_SDU_SIZE=244 -DCONFIG_APP_CONN_INT_UNITS=12` | Test A′ CoC-244 (CoC-vs-GATT range) |
| `gatt-central-244-15ms.hex` | `z54-gattdl-central` `-DCONFIG_APP_CONN_INT_UNITS=12` | Test A′ GATT-244 |
| `gatt-peripheral-244.hex` | `z54-gattdl-dk` (default) | Test A′ GATT peripheral |
| `coc-duplex-central-480-15ms-fsu-on.hex` | `coc-duplex-central` `open-fsu.conf` same -D | CoC-duplex re-check → ~181 KB/s (stall gone) |
| `coc-duplex-sink.hex` | `coc-duplex-sink` (has `AUTO_DATA_LEN_UPDATE=y` → DLE=251 trigger) | CoC-duplex re-check peripheral |
| `coc-central-480-15ms-fsu-on-deepbuf96.hex` | `coc-central` `open-fsu.conf;deepbuf.conf` same -D | §11.1 refill test (96 TX buf → same ~10 PDU/ev) |

Notes:
- The FSU interval sweep (25/37.5/50 ms) rebuilds `coc-central` with `-DCONFIG_APP_CONN_INT_UNITS=20/30/40`
  (not all archived as hexes — trivial rebuilds).
- The `coc-duplex-*` apps are the buildable reconstruction of
  `debug-evidence/coc-duplex-artifact-20260814/firmware/cocdx-*` that **reproduced ~181 KB/s** and
  showed the 08-14 stall does not recur.
- `deepbuf.conf` (in `coc-central/` and `coc-sink/`) is the deep-buffer overlay; it did NOT move
  PDU/event (§11.1 — depth is not the refill lever).

## Latency-under-load rig (coclat, §11.2)
`coclat-central` (stop-signal ping-pong on the same CoC channel as bulk) + `coclat-sink` (echo).
**Pin the interval** (sink PREF=12 + `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`, set in coclat-sink) or
latency drifts. Central knobs: `-DCONFIG_APP_POOL_DEPTH=<n>` (64=saturating, 4=paced),
`-DCONFIG_APP_NO_BULK=y` (idle), `-DCONFIG_APP_OFFERED_KBPS=<n>` (0=saturate).

| hex | config | result |
|---|---|---|
| `coclat-sink-pinned15ms.hex` | sink, PREF=12, no auto-update | pairs with all coclat centrals |
| `coclat-central-idle.hex` | NO_BULK | idle 24.8 ms (FSU-neutral, ==GATT) |
| `coclat-central-pool64.hex` | saturating deep pool | ~290 ms under load |
| `coclat-central-pool4.hex` | completion-paced | ~107 ms (still >budget) |
| `coclat-central-offered-{40,80,120}.hex` | below ceiling | ~10 ms, 0% >30ms (delivered 1:1) |
| `coclat-central-offered-{160,0}.hex` | at/over ceiling | ~273–290 ms, 100% >30ms |

**The knee is AT the ceiling**: below ~120 KB/s the stop-signal is ~10 ms; at saturation ~290 ms.
