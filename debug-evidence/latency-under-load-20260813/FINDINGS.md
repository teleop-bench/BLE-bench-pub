# Latency-under-load — 2026-08-13

**Question:** the customer's safety link must carry a low-latency **stop-signal**
*concurrently* with bulk telemetry. Every prior benchmark measured latency **or**
throughput in isolation. Here we measure the **stop-signal RTT (and its tail) while
bulk saturates the same connection** — the actual requirement.

**Rig:** nRF54L15-DK pair. Central drives a serialized GATT ping-pong (8-byte write →
notify echo = stop-signal RTT) on the ping char, while a *separate* thread token-buckets
244-byte bulk writes to a distinct **bulk-sink** char (counted, never echoed) at a
stepping load 0→150 KB/s (60 s/stage). 2M PHY, DLE 251.
**Caveat:** RTT is measured app-API→callback, so it includes host scheduling, not pure
on-air time. Loss/availability is exact (probe accounting).

## 1. Baseline — 7.5 ms interval, un-paced bulk (2 reset-isolated reps, agree)

| bulk load | stop-signal RTT mean | % of stops >30 ms | worst (windowed max) |
|-----------|:--------------------:|:-----------------:|:--------------------:|
| 0 (idle)  | ~12 ms               | ~0.5–2 %          | 42–50 ms (startup)   |
| 25 KB/s   | 14 ms                | 7 %               | **100–175 ms**       |
| 50        | 16 ms                | 17–18 %           | 76–175 ms            |
| 75        | 19 ms                | 30–31 %           | 66–87 ms             |
| 100       | 23–25 ms             | 47–50 %           | 57–79 ms             |
| 125       | 29–30 ms             | 72–75 %           | 56–117 ms            |
| **150 (saturated)** | **35 ms**   | **99.5–99.7 %**   | 49–64 ms             |

**Headline: on a single shared connection, bulk traffic wrecks the safety-message
latency.** Idle ~12 ms → saturated ~35 ms mean, with *essentially every* stop-signal
exceeding 30 ms. **Zero pongs lost** across 20 k+ probes per rep — nothing drops, it all
*queues*. Classic head-of-line blocking. Note the worst-case spikes are largest at
**light** load (100–175 ms at 25–50 KB/s): rare stop-signals get stuck behind a bulk
burst → high-variance outliers even when average load is low. **The naive
"one link, share it" architecture fails the safety requirement.**

## 2. Mitigation — completion-pace the bulk to 1-outstanding ("polite bulk")

Same 7.5 ms rig; the only change is the bulk sender waits for each write's completion
callback before issuing the next (≤1 bulk write in the TX path at a time).

| bulk load | mean RTT (un-mit → **polite**) | % >30 ms (un-mit → **polite**) | bulk delivered |
|-----------|:------------------------------:|:------------------------------:|:--------------:|
| 50        | 16 → **15 ms**                 | 17 % → **1.9 %**               | ~50 KB/s       |
| 100       | 23 → **18 ms**                 | 47 % → **4.2 %**               | ~100 KB/s      |
| **150**   | 35 → **21 ms**                 | **99.7 % → 2.5 %**             | **~140 KB/s**  |

**The insight: it's queue *depth*, not throughput, that kills latency.** Un-paced bulk
fills the whole host TX buffer pool (~10 deep), so the stop-signal waits behind a deep
backlog. Capping the bulk to **1 outstanding** keeps the queue shallow — the stop-signal
is serviced within ~1 event — while the bulk **still saturates the air** (it refills
immediately after each completion). Result: **~140 KB/s bulk AND severe (>30 ms)
stop-signal violations cut from ~100 % to ~2.5 % at saturation, on a single connection**
(no second radio needed). **Not a hard guarantee:** the mean still rises 12→21 ms and
rare ~100–130 ms spikes persist — a *hard* bound needs a separate connection or
controller-level priority.

## 3. 1 ms (1.25 ms) interval — the sub-ms premium

Idle at 1.25 ms is excellent: **RTT ~1.9 ms, max ~3 ms, zero excursions >5 ms** (199
probes/s). **But it is fragile under concurrent bulk:** at just 25 KB/s the RTT already
degraded (mean 3.1 ms, **max 44 ms**), and ~5 s into the load the **central crashed** —
`USAGE FAULT / ZEPHYR FATAL ERROR 36` ("fault during interrupt handling") at t≈60 s;
the periph saw the 0x08 supervision-timeout drop. This is a **known-brittle operating
point** (the firmware header documents 1 ms-regime crashes). So on this open stack the
sub-ms interval is great for *idle* stop-signal latency but **not viable for
concurrent bulk without first fixing the fault** — the robust choice is 7.5 ms +
polite-bulk.

## Customer takeaways

1. **Don't share one plain connection** between telemetry and the stop-signal — the stop
   inflates 12→35 ms (and spikes to 100+ ms) the instant bulk flows.
2. **Cheap, effective single-link fix:** completion-pace the bulk (1-outstanding). Keeps
   ~140 KB/s throughput and cuts >30 ms stop-signal violations ~40× (100 %→2.5 %).
3. **For a hard latency bound:** separate connection / L2CAP priority (not yet measured).
4. **Sub-ms interval** buys a superb idle stop-signal (~1.9 ms) but is currently unstable
   under load on the open stack — 7.5 ms is the robust operating point today.

## Files
- `logs/` — 2×7.5 ms reps, mitigation (central+periph), 1 ms (central crash + periph).
- `firmware/` — flashed HEX for each build (provenance).
- `app-configs/` — prj.conf + loadramp.conf + pc-1ms.conf. Central code deltas vs the
  ping-pong app: connect at 7.5 ms (`BT_LE_CONN_PARAM(6,6)`), `APP_TPUT_BLAST` off (ping
  thread runs RTT), periph `APP_TPUT_SINK` off (echo ON so pings get pongs). Mitigation
  build: `load_ramp_fn` uses `bt_gatt_write_without_response_cb` + a 1-permit sem.
- `analyze.py` — per-stage parser. `capture-tool.py` — pyserial+DTR reader.
