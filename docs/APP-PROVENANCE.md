# App provenance map — which app produced which result

**Read this before mapping a result to an app.** The app that produces a result is **not** always the
one whose name matches — e.g. the headline GATT throughput/FSU result comes from `z54-lat-central` (a
*latency*-named app), not from the apps literally named `z54-gatt-*`. Always follow the **provenance
chain**: prebuilt hex → `prebuilt-hexes/**/README.md` + `SHA256SUMS` → the `REPRODUCE.md` recipe →
app + overlays. This file is the canonical, one-stop version of that chain. (Lesson: `docs/LESSONS.md`,
"map by provenance, not name.")

## Hardware / stack legend
| tag | silicon | BLE | Zephyr | FSU? |
|---|---|---|---|---|
| **nrf52 (Zephyr 2.7)** | nRF52840 dongle + nRF52832 DK | 5 | 2.7 (PlatformIO) | **N/A — nRF52 HW/firmware does not support FSU** (a BLE 6 feature) |
| **z44** | nRF52840 dongle + nRF52832 DK | 5 | 4.4.1 | **N/A — same nRF52 HW** (z44 = z54 source built for nRF52) |
| **z54 / coc / eatt / q2** | 2× nRF54L15-DK | 6 | 4.4.1 (`fsu-m0` fork) → rebased to 4.4.2 (`fsu-m0-v442`) | **Yes — FSU-capable** |

A "floor-less" (no `EVENT_IFS_LOW_LAT_US`/`CONN_INTERVAL_LOW_LATENCY`) config on any **nrf52/z44** app is
**expected and correct**, never a bug — those are BLE 5 bandwidth/latency benchmarks.

## Headline (nRF54L15 / BLE 6) results → app
| result | central app | peripheral app | overlays (FSU on) | recipe |
|---|---|---|---|---|
| CoC one-way throughput + FSU | `apps/coc/coc-central` | `apps/coc/coc-sink` | `open-fsu.conf` (both) | REPRODUCE §CoC one-way (L167) |
| CoC duplex + FSU | `apps/coc/coc-duplex-central` | `apps/coc/coc-duplex-sink` | `open-fsu.conf` (both) | REPRODUCE L223 |
| GATT one-way throughput + FSU | `apps/nrf54l15/z54-lat-central` | `apps/nrf54l15/z54-lat-periph` | `tput-open.conf;fsu-open.conf;hh-fsu52.conf` (cen) · `tput-open.conf;fsu-open.conf` (per) | REPRODUCE §GATT one-way (L145) |
| GATT duplex (aggregate) | `apps/nrf54l15/z54-lat-central` | `apps/nrf54l15/z54-lat-periph` (**echo**) | `tput-echo-open.conf;fsu-open.conf` (per) | REPRODUCE §GATT duplex (L160) |
| CoC latency-under-load | `apps/coc/coclat-central` (& `coclat2-central` for the 2-PSM lane) | `apps/coc/coclat-sink` (`coclat2-sink`) | — | REPRODUCE §11.2/11.3 (L238) |
| GATT latency-under-load | `apps/nrf54l15/z54-lat-central` | `apps/nrf54l15/z54-lat-periph` | `loadramp.conf;gdeep.conf` (+`APP_LOAD_POLITE=y` paced) | REPRODUCE L253 |
| CoC-vs-GATT downlink (244 B) | `apps/nrf54l15/z54-gattdl-central` | `apps/nrf54l15/z54-gattdl-dk` | (FSU-off control arm) | REPRODUCE Test A′ (L215); `coc-technical-overview.md` Test A′ |
| EATT latency arm | `apps/eatt/eattlat-central` | `apps/eatt/eattlat-periph` | `eatt.conf` (on) / `eatt-off.conf` (off) — **EATT toggle, not FSU; runs at IFS=150 by design** | REPRODUCE §EATT (L289) |
| On-air FSU tIFS (1M & 2M formal accept) | `apps/misc/q2-central`, `q2-central-2m` | `apps/misc/q2-periph`, `q2-periph-2m` | `fsu-f52.conf`/`fsu-f150.conf` (cen) · `fsu.conf` (per) | `debug-evidence/q3-2m-accept-20260906/` |

## Pre-FSU (nRF52 / BLE 5) results → app  — genuine earlier bandwidth/latency benchmarks
| result | central app | peripheral app | documented in | evidence |
|---|---|---|---|---|
| GATT notify uplink throughput (4.4.1) | `apps/z44/z44-gatt-central` | `apps/z44/z44-gatt-dk` | `apps/nrf52/nrf52-l2cap-echo/UPLINK-RECONNECT-FINDINGS.md` §GATT vs CoC (uplink ~44→~154) | inline (measured 2026-07-30, pre-dates `debug-evidence/`) |
| GATT WwR downlink throughput (4.4.1) | `apps/z44/z44-gattdl-central` | `apps/z44/z44-gattdl-dk` | same doc (downlink ~155; CoC≈GATT) | inline |
| CoC throughput, Zephyr 2.7.1 → 4.4.1 + `seg_recv` (nRF52, BLE 5) | `apps/nrf52/nrf52840-l2cap-central` (2.7) · `apps/z44/z44-central` (4.4.1) | `apps/nrf52/nrf52-l2cap-echo` (2.7) · `apps/z44/z44-dk-sink` (4.4.1) | `apps/nrf52/nrf52-l2cap-echo/ZEPHYR-4x-REVISIT.md` (~117 → ~150 KB/s at 480 B) | inline (pre-dates `debug-evidence/`) |
| CoC uplink + central-reboot reconnect wedge (4.4.1) | `apps/z44/z44-uplink-central` | `apps/z44/z44-uplink-dk` | `apps/nrf52/nrf52-l2cap-echo/UPLINK-RECONNECT-FINDINGS.md`, `zephyr-bug-report.md` (minimal-repro attempt: `apps/z44/z44-repro-*`) | inline |

## Untested builds (no result)
- `apps/nrf54l15/z54-gatt-central` and `apps/nrf54l15/z54-gatt-dk` — the nRF54L15 build of the
  BLE-5 GATT-**notify** (uplink) throughput apps `z44-gatt-central`/`z44-gatt-dk` (byte-identical
  source). No result in this repository comes from this pair.
