# Same-bench duplex head-to-head (open vs SDC, ±FSU) — 2026-08-14

GATT echo duplex (central blasts writes to the echoed char; periph echoes each as a notify → the
same stream carries downlink write + uplink echo). Aggregate = central exkBps (uplink) + periph
rxkBps (downlink). 2M, same bench, same day.

| regime | open-off | SDC-off | open+FSU | SDC+FSU |
|--------|----------|---------|----------|---------|
| 7.5 ms | 178      | 176     | 176 (52µs) | 178 (70µs) |
| 12.5 ms| 171      | 172     | 173 (52µs) | 174 (70µs) |

- **Symmetric**: uplink ≈ downlink (~88–89 KB/s each) every cell.
- **PARITY**: open ≈ SDC within ~1–2% in every cell.
- **FSU gives NO duplex gain** at either interval (both controllers) — duplex already saturates the
  air in BOTH directions (write+echo interleaved), so there is no event-fill headroom for FSU's
  freed air-time to convert (contrast the one-way CoC result where open's event had headroom at
  12.5 ms → +20%). Duplex is ~flat/slightly lower at 12.5 ms than 7.5 ms.
- **The historical SDC+FSU duplex +14% (→200) did NOT reproduce same-bench** — it was already flagged
  "exceeds the airtime model, mechanism unresolved" in the records; same-bench it is FSU-neutral.

## Bottom line
Duplex: **~176–178 KB/s aggregate, symmetric, open ≈ SDC parity, FSU-neutral.** The controllers tie;
FSU's throughput benefit is a one-way/event-fill-headroom phenomenon and does not appear in the
already-saturated duplex regime.
