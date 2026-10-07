# Latency-under-load — hardening pass (2026-08-14, n=2, per-stage percentiles)

Re-run of the 2026-08-13 latency-under-load campaign to address a review: the original
mitigation curve was ~n=1 and the tail was reported as a single-observation windowed max.
This pass adds **n=2 reset-isolated reps per arm** and a **per-stage 1 ms-bin RTT histogram**
so the tail is a stable **p50/p99/p99.9**, not a lucky/unlucky max.

## Rig (same as parent, 7.5 ms / 2M)
2× nRF54L15-DK. Central drives a serialized GATT ping-pong stop-signal (8 B write → notify echo)
while a separate thread token-buckets 244 B bulk to a distinct sink char, stepping offered load
0→150 KB/s (60 s/stage). Two arms:
- **naive** — fire-and-forget bulk writes (`bt_gatt_write_without_response`).
- **paced** — completion-paced to 1-outstanding (`_cb` + 1-permit sem), `CONFIG_APP_LOAD_POLITE=y`.

Firmware built from `z54-lat-central` (histogram + `APP_LOAD_POLITE` added this session) with the
parent dir's `app-configs/central-prj.conf;central-loadramp.conf`. Percentiles computed on-device
from the histogram and emitted as one `PCTL target=... p50=.. p99=.. p99.9=.. max=.. g30=.. g100=..`
line per stage (a single printk — many small printks flood CONFIG_LOG and drop the line).

## Result (n=2 average; `latency-vs-load-percentiles-20260814.csv`)

| offered load | naive p50 / p99 / p99.9 | naive >30 ms | paced p50 / p99 / p99.9 | paced >30 ms |
|---:|:---:|:---:|:---:|:---:|
| 0 (idle) | 11 / 19 / 26 | 0.1% | 11 / 19 / 19 | 0.0% |
| 25 | 11 / 40 / 49 | 7.0% | 11 / 24 / 30 | 0.2% |
| 50 | 11 / 44 / 56 | 17.3% | 11 / 26 / 29 | 0.1% |
| 75 | 11 / 44 / 58 | 29.9% | 11 / 26 / 31 | 0.2% |
| 100 | 18 / 52 / 71 | 47.4% | 19 / 26 / 32 | 0.3% |
| 125 | 33 / 55 / 67 | 79.1% | 19 / 27 / 34 | 0.4% |
| **150 (sat)** | **34 / 60 / 74** | **99.9%** | **19 / 28 / 34** | **0.5%** |

**Zero timeouts and zero disconnects across all 4 runs** — nothing drops, it queues (head-of-line
blocking). Completion-pacing holds p99 essentially flat (~26–28 ms) and >30 ms under 0.5% at full
saturation, vs. naive p99 ~60 ms / >30 ms ~99.9%. Confirms the parent finding with a stable tail:
**it's queue depth, not throughput, that sets the latency tail.** Strong single-connection
mitigation; still not a hard bound (a hard worst-case bound needs a separate connection / LL
priority — not measured).

## Bounds / caveats
- RTT is app-API→callback (includes host scheduling), not on-air. Percentiles are 1 ms-resolved.
- Bulk **delivered** throughput not re-captured this pass (periph sink counter not logged); the
  parent campaign measured ~140 KB/s delivered for the paced arm at the 150 target.
- Same-RF-day; 7.5 ms interval only (the 1 ms arm's under-load crash is unchanged, see parent §3).

## Files
`latency-vs-load-percentiles-20260814.csv` (n=2 table), `logs/` (4 raw central captures, full PCTL),
`firmware/` (naive+paced+periph hexes + resolved .config), `SHA256SUMS`.
