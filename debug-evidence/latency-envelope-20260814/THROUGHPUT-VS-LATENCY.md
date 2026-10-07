# Measured throughput-vs-latency envelope (open nRF54L15, CoC + FSU)

Both axes MEASURED (latency was previously estimated). Throughput = FSU-on receiver-delivered
goodput from the interval sweep (`../coc-open-fsu-20260813/`). Latency = idle app round-trip
(GATT write→notify ping-pong, app-API→callback, z54-lat rig), pinned at each interval (peripheral
auto-update disabled so the interval holds), 0 timeouts every arm.

| interval | FSU throughput KB/s | no-FSU KB/s | RTT mean (ms) | RTT min | RTT max | RTT/interval |
|----------|---------------------|-------------|---------------|---------|---------|--------------|
| 7.5 ms   | 143.6               | 144.6       | 12.2          | 11.4    | 26.9    | 1.63×        |
| 10 ms    | 156.1               | 133.5       | 17.4          | 16.5    | 56.9    | 1.74×        |
| **12.5 ms** | **173.6**        | 139.8       | **23.1**      | 14.3    | 86.9    | 1.85×        |
| 15 ms    | 170.5               | 145.5       | 27.9          | 25.5    | 86.9    | 1.86×        |
| 20 ms    | 172.4               | 141.8       | 39.1          | 24.4    | 96.9    | 1.96×        |
| 30 ms    | 162.8               | 142.6       | 58.5          | 56.5    | 146.9   | 1.95×        |

## Findings
- **Latency scales ~1.6–2× the interval** (grows from 1.63× at 7.5 ms to ~2× at 30 ms). The
  earlier estimate (~1.6× flat) was ~15–20% LOW at the longer intervals — measuring mattered
  (12.5 ms is 23 ms, not the estimated ~20 ms; 20 ms is 39 ms, not ~32 ms).
- **The frontier has a knee at 12.5 ms: 173.6 KB/s for 23 ms RTT.** Below it you sacrifice
  throughput; above it you pay latency for none (throughput plateaus then declines while RTT keeps
  climbing linearly).
- **7.5 ms** is the min-latency corner: 12.2 ms RTT, but no FSU throughput gain (~144).
- Every point is far inside the ~300–700 ms stop budget — even 30 ms interval (58 ms RTT) has
  ~5–12× margin. So latency is not the binding constraint; throughput is.

## Operating-point guidance
- **Throughput-optimal (bulk):** 12.5 ms + FSU → 173.6 KB/s @ 23 ms RTT — the frontier knee.
- **Latency-optimal (tight safety loop):** 7.5 ms → 12 ms RTT, ~144 KB/s (FSU no help here).
- **Balanced:** 15 ms + FSU → 170 KB/s @ 28 ms RTT.

Caveats: RTT is app-API→callback (includes host scheduling on both ends), IDLE (no concurrent bulk;
under load the tail degrades — see [[latency-under-load]]). FSU does not change latency (interval-
bound), confirmed. Throughput on same-day RF ~10 KB/s under historical 157; relative shape robust.
