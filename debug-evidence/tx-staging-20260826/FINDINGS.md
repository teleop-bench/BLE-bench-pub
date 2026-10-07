# TX-staging investigation — Stage 1 + 1b results (2026-08-26)

Rig: 2× nRF54L15-DK, 10 cm, open Zephyr (ll_sw_split, fsu-m0 @ v4.4.1-15). Central downlink,
CoC SDU=480 B, seg_recv sink. Metric = `mean_pkts/ev` (controller aired PDU/event, from the
gated event-fill diag) + sink KB/s. Sink built `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` so the central's
pinned interval holds (verified per run via `GATE conn: interval=`). eagain=0, poolfail=0 throughout.

## Stage 1 — interval + buffer sweep
| arm | interval | buffers (ACL_TX/CONN_TX) | mean PDU/ev | max | PDU/ms | sink KB/s |
|-----|----------|--------------------------|-------------|-----|--------|-----------|
| A1  | 7.50 ms  | 64 / 64 (deep)           | 4.87        | 5   | 0.65   | 156.7 |
| A2  | 15.00 ms | 64 / 64 (deep)           | 9.64        | 10  | 0.64   | 151.7 |
| A3  | 25.00 ms | 64 / 64 (deep)           | 16.30       | 17  | 0.65   | 158.3 |
| B1  | 15.00 ms | 8 / 8 (shallow)          | 9.70        | 10  | 0.65   | 155.3 |

- **PDU/event scales linearly with interval** (~0.65 PDU/ms = ~650 PDU/s), while **delivered
  throughput is flat ~155 KB/s** across intervals. With deep buffers the host preloads up to 64
  PDUs, so the controller is never host-starved — yet it airs only what fits the interval's airtime.
- A fixed per-event *count* cap would be flat vs interval; a host-staging *refill* throttle would be
  flat too (fixed handoff rate) OR buffer-sensitive. Neither: it grows with airtime, and deep(64) ≡
  shallow(8) at 15 ms (9.64 vs 9.70). → **airtime-bound, not staging/buffer/credit-bound.**

## Stage 1b — FSU on/off at matched 15 ms / deep / 10 cm (ABAB order, FSU armed spacing=52)
| arm order | FSU | mean PDU/ev | sent SDU/10s |
|-----------|-----|-------------|--------------|
| A (fon)   | 52  | 11.55       | 3910 |
| B (foff)  | off | 9.65        | 3283 |
| C (fon)   | 52  | 11.44       | 3921 |
| D (foff)  | off | 9.68        | 3282 |

- **FSU alone lifts PDU/event +18.6%** (9.66 → 11.50) and sent SDUs +19%. Order-stable (arms ran
  ABAB — ON/OFF/ON/OFF; note this is A-B-A-B, not the drift-cancelling A-B-B-A ABBA).
- tIFS-airtime model (on-air fields, corrected): per-pair cost ≈ **1048 µs** (251-octet PDU @ 2M) +
  tIFS + **~44 µs** empty ACK + tIFS. tIFS 150 µs ⇒ **~1392 µs**; 52 µs ⇒ **~1196 µs**. **Integer
  packing:** a 15 ms event fits **10 pairs at 150 µs → 12 at 52 µs = +20%**; the ~16% continuous
  per-pair saving quantizes up to that 10→12 step ⇒ measured **+18.6%**. Consistent.
- FSU changes **only** the inter-packet tIFS gap, and the change is +18.6% — consistent with the
  binding constraint being the tIFS inter-packet gap (airtime), and inconsistent with a tIFS-invariant
  staging throttle.

## Verdict
**Three converging observations, anchored by the exact physical-capacity match** (`max = 5/10/17` pairs
at 7.5/15/25 ms equals `floor(interval / 1392 µs)` exactly), show the central-downlink
"**~10 PDU/event refill wall**" is the **tIFS-bounded airtime ceiling**, NOT a per-event
host→controller staging throttle — *within the tested 7.5–25 ms / 2M / 251 B regime*. (The interval-
scaling and host-pulls≈aired observations each converge with this but are not independently
discriminating — see `coc-technical-overview.md §11.1` for the caveats.) The earlier §11.1 "refill limit = per-event TX staging,
not airtime" framing compared a 15 ms occupancy (~10) against a ~35–41 airtime *model* that is
really a ~50 ms figure (0.65 PDU/ms × 50 ms ≈ 32) — an interval mismatch.

**Consequence for the shared-root thesis:** FALSIFIED on the downlink side. There is no downlink
staging throttle for the peripheral-uplink stall to share a root with. The peripheral DLE=251 stall
is therefore a **separate** bug (characterized in Stage 2).

Raw logs: s1a1/s1a2/s1a3/s1b1_cen.log (Stage 1); s1_fon_A/foff_B/fon_C/foff_D_cen.log (Stage 1b).

## Stage 2 — direct host-handed counter (l2cap_pull_pdus, BT_TESTING) + stall probe
**Run A (downlink, 15 ms deep):** host-handed PDUs/event vs controller-aired PDUs/event:
| window | aired mean_pkts/ev | host_pulls/ev |
|--------|--------------------|---------------|
| 1 | 9.35 | 9.40 |
| 2 | 9.68 | 9.67 |
| 3 | 9.69 | 9.68 |

Host hands the controller **exactly what airs, 1:1** — no surplus queued-but-unaired. With deep
buffers free to stage 64, the pull is airtime-paced (controller pulls the next PDU only as it
finishes airing the last). Direct confirmation of the airtime-bound picture; rules out a controller
per-event cap with host oversupply.

**Run B + Stage 2b (peripheral DLE=251 uplink, 4 channel-open events):** the documented stall
(`UP sent` frozen ~128, `txcred` frozen 64, uplink=0) **did NOT reproduce.** Every time, uplink
flowed (~87 KB/s duplex @7.5 ms), `UP sent` climbed, `host_pulls ≈ UP sent`, `txcred=0` (draining):
| event | UP sent (grew) | host_pulls | txcred |
|-------|----------------|------------|--------|
| Run B  | 497 → 13166 | 437 → 13415 | 0 |
| cyc 1  | 4802 → 5171 | 4848 → 5225 | 0 |
| cyc 2  | 4597 → 4966 | 4637 → 5015 | 0 |
| cyc 3  | 4966 → 5376 | 5018 → 5437 | 0 |

At `tx_max=251` the peripheral host stages uplink PDUs normally (`host_pulls` tracks `UP sent`). So
the stall is **NOT deterministically triggered by DLE=251** (contra the earlier "251→stalls" note);
it is intermittent and did not manifest across 4 fresh channel-opens.

## FINAL VERDICT — shared-root thesis FALSIFIED (within the tested 7.5–25 ms / 2M / 251 B regime)
1. **Downlink "refill wall" = tIFS-bounded airtime**, not a per-event host-staging throttle.
   **Anchored** by the exact capacity match: measured `max = 5/10/17` pairs at 7.5/15/25 ms equals
   `floor(interval / 1392 µs)` exactly (251-octet PDU ≈ 1048 µs @ 2M + 44 µs empty-ACK + 2×tIFS),
   and FSU (tIFS→52 µs, cycle ~1196 µs) packs 12 vs 10 pairs at 15 ms (+20% integer ≈ measured
   +18.6%). Two further observations **converge** (each consistent-with but not solely discriminating):
   aired ∝ interval under deep preload; host hands ~what airs ~1:1. ("~35–41 wall" was a ~50 ms figure.)
2. **Peripheral uplink at DLE=251 stages normally** in the four opens tested (host_pulls≈UP sent);
   the documented stall did not reproduce → **no persistent staging throttle in those opens** (not a
   proof that none can occur).

No *persistent* per-event host→controller staging throttle was observed in either direction across
the tested opens/regime. The premise "one
staging fix lifts the whole ceiling" dissolves: the ceiling is **airtime** (moved only by
FSU/PHY/packing), and the "stall" is a separate, elusive intermittent bug. This is the clean,
honest kill the derisking staged for — no multi-day controller-internals fix is warranted.
