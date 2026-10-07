# SDC vs open head-to-head: the 174-vs-178 gap is a measurement-regime artifact, not a controller difference

**2026-08-14, same bench, same day, identical app source per regime.** The apparent "open 174 vs
SDC 178" gap came from comparing different transports, intervals, AND days. Measured head-to-head:

## Regime A — GATT blast, 50 ms, 2M (the SDC's OWN 178-benchmark regime; app = z54-lat TPUT_BLAST)
Receiver-side rxkBps (steady, t≥22 s), FSU spacing confirmed on air, 0 disconnects, interval=50000 held:

| controller | no-FSU (mean/max) | +FSU (mean/max) | FSU tIFS | FSU gain |
|------------|-------------------|-----------------|----------|----------|
| **open**   | 127 / 152         | **143 / 166**   | 52 µs    | +12 %    |
| **SDC**    | 125 / 151         | **145 / 166**   | 70 µs    | +16 %    |

**PARITY** — open+FSU and SDC+FSU both peak at **166**, means within ~2 %. Open even runs the
shorter tIFS (52 vs 70 µs). Neither controller has a throughput edge in this regime.

Note: today's absolutes (mean 145 / max 166 with FSU) sit ~11–18 % BELOW the historical committed
SDC one-way (mean 178 / max 186, `sdc-bench-20260806/u1`) — an RF-day effect (both controllers
depressed proportionally; the same day my CoC open baseline was ~145 vs the historical 157). The
PARITY is the robust same-bench result; the absolute is RF-dependent.

## Regime B — L2CAP CoC, short intervals, 2M (my envelope regime; app = coc-cen/sink)
| interval | open+FSU | SDC+FSU |
|----------|----------|---------|
| 7.5 ms   | 145      | 138     |
| 12.5 ms  | **167**  | 138     |

open scales with interval (CoC); SDC-CoC measured flat ~138. CAVEAT: the SDC-CoC arm used ad-hoc
SDC buffer tuning (coc app), NOT the tuned bench overlays that Regime A used — so 138 likely
UNDER-sells SDC in CoC; do not read Regime B as "open beats SDC" without a tuned SDC-CoC build.

## Answers to the two questions
1. **"Run open Zephyr + FSU on the 178 config":** open+FSU GATT@50 ms = 143 mean / 166 max —
   **essentially identical to SDC+FSU (145/166) on the same bench.** Open matches the proprietary
   stack in its own regime.
2. **"Run SDC / SDC+FSU on my CoC config":** SDC+FSU CoC@7.5/12.5 ms = 138/138 (see Regime B caveat).

## Bottom line
The 174-vs-178 was comparing **CoC@12.5 ms (normal RF) vs GATT@50 ms (cleaner RF, different day)**.
Head-to-head on one bench, **open ≈ SDC (±2 %)**. So "how do we meet/exceed the proprietary result?"
— **we already match it**; the gap was a regime/RF artifact. Beating the absolute 178 needs a
cleaner-RF bench (today's ceiling was 166), not a controller change.

## UPDATE: Regime B re-run with PROPERLY-TUNED SDC-CoC (deeper RX, EVL=50000) — 2026-08-14
The earlier SDC-CoC 138 WAS under-tuned. With SDC_RX_PACKET_COUNT=20 + EVL=50000 + event-extend,
same bench, interleaved:

| interval | open+FSU | SDC+FSU | verdict |
|----------|----------|---------|---------|
| 7.5 ms   | 146.5    | 146.0   | **PARITY** (SDC fixed: 138→146) |
| 12.5 ms  | **170.9**| 142.9   | **open +20%** |

SDC-CoC is FLAT ~143–146 across intervals even when properly tuned; open-CoC SCALES with interval
(146→171, per the throughput envelope: pkts/event 5→10). So at the CoC/12.5 ms operating point open
genuinely BEATS the tuned SDC by ~20% — the SDC's CoC event doesn't fill longer intervals the way
open's does. (n=1/cell; 1 transient disconnect on open@7.5 ms.)

## FINAL cross-matrix (all same-bench, same-day, FSU on, tuned both sides)
| regime | open+FSU | SDC+FSU | winner |
|--------|----------|---------|--------|
| GATT blast, 50 ms   | 143 (max 166) | 145 (max 166) | parity |
| CoC, 7.5 ms         | 146           | 146           | parity |
| CoC, 12.5 ms        | **171**       | 143           | **open +20%** |

**Answer to "meet or exceed the proprietary result": we MATCH it in its own regime (GATT/50 ms and
CoC/7.5 ms), and EXCEED it by ~20% at the CoC/12.5 ms operating point** — open's event-fill scales
with the interval; the SDC's CoC path plateaus. The original 174-vs-178 was a regime/RF artifact
(parity in GATT/50 ms). Open runs the shorter tIFS throughout (52 µs vs SDC's 65–70 µs).

## HARDENED: CoC/12.5 ms open-vs-SDC, ABBA×2 (n=4 each, drift-cancelled) — 2026-08-14
Order O S S O O S S O, same bench interleaved, FSU confirmed each arm (open 52 µs / SDC 65 µs),
0 disconnects across all 8:

- **open+FSU: 169.6, 169.9, 170.3, 171.0 → mean 170.2 ± 0.5 KB/s**
- **SDC+FSU:  141.8, 143.3, 142.6, 141.0 → mean 142.2 ± 0.9 KB/s**
- **+28.0 KB/s = +19.7 %.** STRICT non-overlap: min open (169.6) > max SDC (143.3) by 26 KB/s;
  gap ≈ 30× pooled SD. Drift-cancelled by the ABBA×2 layout. **The +20 % is now solid, not n=1.**

Mechanism (from the throughput envelope + head-to-head): open's CoC event fills more as the interval
grows (pkts/event 5→10 from 7.5→12.5 ms); the tuned SDC's CoC event plateaus ~142. So the open
controller genuinely converts the 12.5 ms interval into ~20 % more goodput than the proprietary SDC
at the same operating point, with a shorter tIFS (52 vs 65 µs).
