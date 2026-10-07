# Degraded-RF / range safety sweep — IN PROGRESS (day 1: 2026-09-09)

Rig: coclat-central (near, on Mac, serial 1057794857) + coclat-sink echo (far, power bank, 1057719509),
7.5 ms, 2M PHY, FSU spacing=52, DLE-251. Central logs; sink is a dumb echo (moves freely, never re-flashed).
Two central firmwares (in firmware/): RTT = coclat-central NO_BULK (stop-signal ping-pong only);
TPUT = coclat-central bulk-on. Controller pin fee9fbc (v4.4.2-16).

## Day-1 data points
> **External LOS reference:** published clean-LOS BLE range numbers (Nordic/Argenox/Sheridan/etc., with source URLs) are in [`docs/references/ble-los-range-refs.md`](../../docs/references/ble-los-range-refs.md). Bottom line there: our ~12 m-solid/~24 m-drop through 3 walls + metal is consistent with a 2M/0 dBm link whose *clean* LOS range is ~25–50 m indoor / ~100 m-class outdoor. We are NOT running a clean-LOS test for now.

> **ENVIRONMENT NOTE:** the ≥40 ft / 3-wall points also had a **large metal object in the RF path** (discovered mid-walk). Metal = strong attenuation + multipath, far harsher than drywall — so the absolute distances here are PESSIMISTIC (clean LOS / drywall-only would reach further). Treat this as a harsh, metal-inclusive path (a reasonable shipyard proxy), not a clean range number. Behavior/shape transfers; absolute feet do not.

| distance | stop-signal RTT (no bulk) | loss | throughput (sat bulk) | ping RTT under sat bulk |
|---|---|---|---|---|
| 10 cm (baseline) | 9.8 ms mean / ~11 max | 0 | **~186 KB/s** | ~189 ms (shared-lane cliff) |
| ~mid-room | ~10.5 ms / 17–25 max | 0 | — | — |
| **40 ft + 1 wall** | **11.9 ms / 69.8 max, ~1.8% >30 ms** | **0** | **~139 KB/s** | **~350 ms (shared-lane cliff, worse than 290 @10cm)** |

| **40 ft + 3 walls** | **13.7 ms / 40 max, 3.6% >30 ms** | **0** | **~63 KB/s** | **~240–490 ms + 17 timeouts (loss appears under sat bulk)** |

| **3 walls + metal · ~80 ft** | **LINK DROPPED** (pings=0 → timeouts → GAP disconnected 0x08 supervision timeout) | — | — | edge: safe failure (loss-of-link = STOP) |

**RANGE EDGE (3 walls + metal obstruction): solid at 40 ft, DROPS by ~80 ft** — clean supervision-timeout disconnect (safe failure mode). Loss-onset distance between 40–80 ft not pinned (capture-open reset the central; caught the ~80 ft/3-walls+metal endpoint).

Graceful degradation: LL retransmits absorb RF hits (tail grows) but 0 packets lost, 0 disconnects @40ft+wall.
TODO day 2: continue walking (more walls / further) to find (1) where loss/timeouts first appear,
(2) the disconnect/drop range; and measure a proper 10 cm THROUGHPUT baseline for an exact range delta.
Caps: caps/*.log (base/sweep = RTT; tput40 = throughput @40ft+wall).

## Throughput range map (CoC/open/FSU/2M/7.5ms, 480B, delivered)
10 cm ~186 → 40 ft/1 wall ~139 (−25%) → 40 ft/3 walls ~63 (−66%) → ~80 ft/3 walls+metal DROP. Safety-tier need ~13 KB/s → ≥5× headroom everywhere the link is up. Baseline log: caps/base-tput.log.

## GATT arm (z54-lat-central RTT ping-pong / z54-lat-periph echo, open/FSU/2M/7.5ms) — day 1
Same rig-design, GATT transport. Central=1057794857 (Mac, logs), periph=1057719509 (moved). Controller fee9fbc.
| position | GATT RTT mean | worst | >30ms | loss | vs CoC (same spot) |
|---|---|---|---|---|---|
| 10 cm | 11.9 ms | ~19 | 0% | 0 | CoC 9.8 ms |
| 40 ft · 1 wall | 12.8 ms | 42 | 0.1% | 0 | CoC 11.9 / 70 / 1.8% |
| 40 ft · 3 walls | 19.1 ms | 80 | 8.5% | 0 | CoC 13.7 / 40 / 3.6% |
| ~80 ft · 3 walls + metal | **ZOMBIE/DEAD** (subscribed but 0 round-trips complete, all timeout) | — | 100% | — | CoC clean 0x08 drop |
GATT runs a HOTTER TAIL than CoC at range (write+notify = 2 LL transactions/RTT vs CoC's 1). Both 0 loss.
Caveat: different pass than the CoC walk → RF-day/placement variance possible; suggestive not locked.
Also confirmed: GATT recovers from a COLD POWER-CYCLE (periph power-off between pos1→pos2 → clean reconnect).
GATT throughput at range NOT measured (delivered lands on the far periph; GATT central has no CoC-style sent proxy).
Firmware/configs in gatt/. Walk in progress.

**GATT edge (~80 ft/3 walls+metal):** 'zombie connected' — link subscribed but 0 stop-signals complete (all timeout); functionally dead → heartbeat/supervision timeout → STOP. CoC failed *cleaner* (immediate 0x08 disconnect). Same usable-range edge (~80 ft) for both transports; GATT lingers useless-but-connected a little longer. Both fail SAFE.

**GATT throughput baseline @ 10 cm: ~175 KB/s** (this session; gatt-fsu-clean rig on_6 = z54-lat-central tput + z54-lat-periph sink, FSU spacing=52) — consistent w/ committed gatt-fsu-clean ~189 within RF-day variance. CoC 10 cm baseline was ~186 (coclat 480B rig). GATT throughput at RANGE not walked (radio-limited → tracks the CoC 186→139→63 curve). Logs: gatt/gtput-*.log.
