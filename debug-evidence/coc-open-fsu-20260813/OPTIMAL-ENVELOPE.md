# Optimal operating envelope: CoC throughput vs connection interval, ±FSU (open nRF54L15)

Interval sweep, 480 B SDU, 2M, receiver-delivered goodput (sink cum_total slope). Every interval
held exactly (GATE conn = requested), 0 disconnects across all 12 arms. Single arm per point
(trend/plateau is the result; 12.5/15/20 ms mutually corroborate).

| interval | latency floor* | no-FSU KB/s | FSU KB/s | FSU gain | pkts/ev off→on |
|----------|----------------|-------------|----------|----------|-----------------|
| 7.5 ms   | ~7.5–12 ms     | 144.6       | 143.6    | −0.7 %   | 5 → 5   |
| 10 ms    | ~10–16 ms      | 133.5       | 156.1    | +16.9 %  | 7 → 8   |
| **12.5 ms** | ~12.5–20 ms | 139.8       | **173.6**| **+24.2 %** | 8 → 10 |
| 15 ms    | ~15–24 ms      | 145.5       | 170.5    | +17.2 %  | 10 → 12 |
| 20 ms    | ~20–32 ms      | 141.8       | 172.4    | +21.6 %  | 14 → 16 |
| 30 ms    | ~30–48 ms      | 142.6       | 162.8    | +14.2 %  | 21 → 24 |

*Latency floor ≈ 1–1.6 connection intervals for an app round-trip (from the latency-under-load work).

## The envelope
- **no-FSU is FLAT ~140–146 KB/s at every interval** — that is plain-Zephyr's CoC airtime ceiling
  (tIFS 150 µs); shortening the interval doesn't raise it (more events/s ⇄ fewer packets/event).
- **FSU lifts the ceiling to a PLATEAU ~170–174 KB/s over 12.5–20 ms**, peaking at 12.5 ms
  (+24 %). It adds ~2 packets/event everywhere; the gain only vanishes at 7.5 ms where 5 packets
  quantize the ~14 % airtime saving to < 1 packet.
- Past ~20 ms the FSU throughput slowly declines (30 ms → 163) — diminishing returns as per-event
  overhead is already amortized and other losses dominate.

## Optimal operating points (pick by latency budget)
- **Max throughput:** **12.5–20 ms interval + FSU → ~170–174 KB/s** (+20–24 % over the ~145 no-FSU
  ceiling). Sweet spot **12.5 ms** (173.6, +24 %).
- **Min latency:** 7.5 ms → ~145 KB/s, FSU gives nothing (spend the airtime on events, not payload).
- **Balanced (safety-link + bulk):** 15 ms + FSU → 170 KB/s at a ~15–24 ms latency floor.

## Bottom line for the objective
FSU DOES beat plain Zephyr on CoC throughput — by **+20–24 %**, reaching ~174 KB/s — but only when
the connection interval is long enough (≥ ~12.5 ms) for events to carry enough packets that the
per-transaction tIFS saving escapes integer-packet quantization. At the conventional 7.5 ms
throughput interval there is no gain. The lever is the interval (a latency trade), not a controller
change (see EVENT-LENGTH-LEVER.md: the 7.5 ms cap is a genuine airtime floor, not a fixable early-close).

Same-day RF caveat: absolutes run ~10 KB/s below the historically-charted 157; the RELATIVE envelope
(flat no-FSU vs FSU plateau +20–24 %) is the robust result.
