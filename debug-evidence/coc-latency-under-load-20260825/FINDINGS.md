# CoC latency-under-load — the backpressure claim, now MEASURED (2026-08-25)

All-CoC rig (new apps `coclat-central`/`coclat-sink`): a serialized tiny stop-signal ping-pong
(20 B PING SDU, echoed) rides the SAME CoC channel as a saturating 480 B bulk blast, on 2×
nRF54L15 @10cm/15ms/2M. Ping shares the tx_pool + credit window with bulk. Report = ping RTT
distribution + bulk rate. This is the CoC equivalent of the GATT latency-under-load rig, and the
measurement the GATT recommendation previously only *assumed*.

## Result — CoC stop-signal is catastrophically stale under CoC bulk load
| config | ping RTT min/mean/max | over 30 ms | bulk KB/s |
|---|---|---|---|
| **pool=64 (deep/naive)** | 264 / **290** / 356 ms | 100% | ~165 |
| **pool=4 (completion-paced analog)** | 98 / **100** / 133 ms | 100% | ~162 |
| GATT saturated (ref, prior) | — / **~35** / ~60 ms | 99.7% | ~140 |

- **CoC under load = ~290 ms** stop-signal RTT (deep pool); the tiny ping queues behind the full
  64-deep bulk pool + credit window (no priority). ~8× worse than GATT saturated (~35 ms).
- **The GATT mitigation (shallow queue / completion-pacing) helps but is NOT enough for CoC:**
  pool 64→4 cut RTT 290→100 ms **with no bulk loss (~162 KB/s)** — but 100 ms is still 100% over
  the 30 ms budget and ~3× GATT. CoC's segmentation (2 PDU/SDU) + credit FIFO + the ~10 PDU/event
  refill limit stack up so a co-channel stop-signal can't reach GATT latency by pacing alone.
- `eagain=0` throughout — this is queue/segmentation head-of-line, not credit exhaustion.

## Bearing on the recommendation
This MEASURES (and quantifies) what §8/§11.2 called design-reasoned: putting the safety
stop-signal on the same CoC channel as bulk makes it 3–8× staler than GATT under load, and even
the best app-level pacing leaves it ~100 ms (>budget). Confirms GATT for the safety/control link —
now on evidence, not just semantics. (To get CoC control to GATT latency you'd need a SEPARATE
CoC channel/connection for the stop-signal — untested; the separate-connection hard bound is the
queued item from latency-under-load.)

## Bounds
Single session @10cm; pool depth is the pacing knob (64 vs 4); FSU-off (fsu=0 — FSU doesn't help
a starved link, §3). A pool-depth sweep (1/2/8/16/32) would draw the full RTT-vs-queue curve.
Apps: `coclat-central` (-DCONFIG_APP_POOL_DEPTH=<n>), `coclat-sink`. Raw logs in logs/.

## Re-verified at PINNED 15 ms interval (2026-08-25) + idle floor
The peripheral was pulling the interval to its 50 ms PREF (drift; idle RTT climbed 24.8->80 ms).
Pinned via sink `PREF_MIN/MAX_INT=12` + `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` (coclat-sink), re-ran:

| condition (pinned 15 ms) | ping RTT mean | over 30 ms |
|---|---|---|
| **idle (no bulk)** | **24.8 ms** (flat, 24.4-25.3) | 0% |
| saturated pool-64 | **~290 ms** (285-339) | 100% |
| saturated pool-4 | **~107 ms** (105-141) | 100% |

- **Idle = 24.8 ms floor** (~1.65x the 15 ms interval, round-trip event wait) — SAME for FSU-on and
  FSU-off (idle_fsu == idle_off == 24.8 ms): **FSU is neutral at idle** (idle RTT is interval-
  dominated, not tIFS). Also ~= GATT idle -> **CoC and GATT are equal at idle; the gap is PURELY
  under load.**
- Saturated numbers held after pinning (queue-dominated, not interval): ~290 (deep) / ~107 (paced).
- Latency decomposition: RTT_under_load ~= idle_floor + (queue_depth x PDU_per_SDU / refill_rate) x
  event_time + echo_return. Deep pool (64 SDU x 2 PDU = 128 PDU / ~10 PDU-per-event) ~= 13 events
  ~= +265 ms -> ~290 ms. The **refill limit (~10 PDU/event, §11.1) is the slow divisor** that turns
  queue depth into latency; segmentation (2 PDU/SDU) doubles it. This unifies §11.1 with the
  latency finding.

## Offered-load curve (unsaturated -> saturated), pinned 15 ms, pool=64
Paced the bulk to a target offered rate (coclat-central `-DCONFIG_APP_OFFERED_KBPS=<n>`, 0=blast):

| offered | delivered | stop-signal RTT | over 30 ms |
|---|---|---|---|
| idle (0) | — | 24.8 ms | 0% |
| 40 KB/s | 38 | 13.3 ms | 0% |
| 80 KB/s | 76 | 9.9 ms | 0% |
| 120 KB/s | 113 | 9.7 ms | 0% |
| 160 KB/s | 153 (ceiling) | 273 ms | 100% |
| saturate | 155 (ceiling) | 290 ms | 100% |

**Sharp KNEE right at the ceiling.** Below ~120 KB/s (< ~78% of the ~155 ceiling): delivered
tracks offered 1:1 AND stop-signal RTT is ~10 ms, 0% over budget. At saturation: RTT cliffs to
~290 ms, 100% over budget. **The danger is SATURATION, not CoC per se** — the actionable safety
rule is to keep bulk offered-load below ~75% of ceiling and the co-channel stop-signal stays safe.
(Sub-idle RTT under moderate load: continuous traffic keeps connection events back-to-back so the
echo rides the next event instead of waiting for a sparse one; idle 24.8 ms -> loaded ~10 ms.)
Knob: `coclat-central -DCONFIG_APP_OFFERED_KBPS=<KB/s>` (0=saturate).

## Matched GATT offered-load curve (from latency-under-load-20260813/hardening-20260814) — fills the GATT unsaturated cell
GATT rig steps offered load 0/25/50/75/100/125/150 KB/s (z54-lat-central APP_LOAD_RAMP), pings
serialized. Extracted PCTL-per-stage:

| offered | GATT-naive p99 / >30ms | GATT-PACED (polite) p99 / >30ms | CoC-unpaced RTT / >30ms |
|---|---|---|---|
| idle | 19ms / 0.1% | 19ms / 0% | 24.8ms / 0% |
| 25-75 | 41-46ms / 7-30% | 23-26ms / <0.2% | ~10ms / 0% |
| 100-125 | 55-56ms / 47-76% | 26ms / <0.6% | ~10ms / 0% |
| 150/sat | 63ms / 100% | 28ms / <0.6% | ~290ms / 100% |

**GATT + completion-pacing NEVER cliffs (p99 <=28ms, <0.6% >30ms at ALL loads incl saturation) —
the robust safety design.** CoC below ceiling is excellent (~10ms) but a knife-edge: cliffs to
~290ms at saturation, ~107ms even paced (segmentation + refill floor). CoC gives lower best-case
latency in the safe zone but ZERO saturation margin; GATT+pacing gives a saturation-robust plateau.
For a safety link, robustness-to-saturation > best-case latency -> GATT+completion-pacing.

## FSU-armed offered-load curve (spacing=52 verified) — FSU widens the safe zone, doesn't kill the cliff
Fixed the coclat FSU-arm bug (requested FSU before PHY=2M+full DLE -> rc=-13; now gate open_l2cap
on DLE tx_max>=251 -> Q3FSU-REQ rc=0, spacing=52). Re-ran offered-load FSU-on:

| offered | FSU-ON deliv/RTT/>30ms | FSU-OFF deliv/RTT/>30ms |
|---|---|---|
| 80  | 76 / 9.8ms / 0%   | 76 / 9.9ms / 0% |
| 120 | 112 / 10.2ms / 0% | 113 / 9.7ms / 0% |
| 160 | 148 / 9.8ms / 0%  | 153 / 273ms / 100% |
| sat | **180** / 249ms / 100% | 155 / 290ms / 100% |

1. **FSU neutral on latency below the knee** (~10ms on/off) — confirmed (saturation-throughput feature).
2. **FSU raises the ceiling +16% (155->180 KB/s)** — throughput benefit only at saturation.
3. **FSU widens the safe zone, doesn't remove the cliff:** at 160 KB/s, FSU-off is AT the ceiling
   -> cliff (273ms); FSU-on is BELOW the new 180 ceiling -> 9.8ms/0%. FSU moves the knee right (more
   bulk headroom before the cliff) but past the higher ceiling it still cliffs (~249ms). FSU buys
   operating margin, not latency immunity. GOTCHA: FSU req before PHY=2M/full-DLE -> -EACCES (see
   REPRODUCE.md); gate on DLE tx_max>=251.

## Separate-channel priority-lane test (§11.3) — WORKS: cliff eliminated
Two-channel rig (coclat2-*): bulk 480B blast on CoC PSM 0x0080 (own pool) + stop-signal ping-pong
on SEPARATE PSM 0x0081 (own pool/credits). Pinned 15ms, FSU armed (spacing=52).

**Control RTT under 179 KB/s UNPACED bulk saturation = ~33ms (min 19, max 69)** — vs ~290ms shared
channel (9x better), ~= GATT-paced (~28ms), ~= idle floor (24.8ms). Zephyr L2CAP/LL interleaves
channels: the tiny control SDU rides its own credit window, NOT the bulk FIFO. The predicted
LL-merge floor did NOT materialize. **BLE CoC has a genuine stack-level priority lane.**

Changes the picture: "dedicated CoC control channel" is a validated safety architecture, ~33ms
under uncontrolled saturation, STACK-guaranteed (no app-layer bulk pacing needed) — the property
GATT+pacing can't offer (needs app discipline, infeasible for bursty/video bulk). For the
uncontrolled-bulk regime this is the first evidence-backed reason to prefer CoC (separate channel).
Open: does a separate GATT characteristic isolate the same way (LL interleave is below L2CAP)?
Apps: coclat2-central/coclat2-sink. Prediction (LL floor ~50-107ms) was wrong, favorably.

## Loop closed: GATT separate CHARACTERISTIC does NOT isolate (no per-char prioritization)
z54-lat rig (separate ping char + separate bulk-sink char, classic GATT = one ATT bearer), deep
ACL queue (ACL_TX=64), naive/unpaced load ramp, pinned 15ms. GATT ping RTT vs offered load:
0->26.9ms, 25-75->14-22ms, 100->28ms, 125->40ms, **150/sat->71ms mean, ~200ms max.**

| control path @ saturation | mean | max | isolated |
|---|---|---|---|
| CoC separate CHANNEL | 33ms | 69ms | YES (stack lane) |
| GATT separate CHARACTERISTIC | 71ms | ~200ms | NO (shared ATT bearer) |
| CoC shared channel | 290ms | - | no |
| GATT completion-paced | 28ms | - | via app-discipline |

**CONFIRMS: classic GATT has no per-characteristic prioritization** — separate characteristics
share the one ATT bearer, so the stop-signal degrades under unpaced bulk (71ms/200ms). The priority
lane is a separate-L2CAP-CoC-CHANNEL property; GATT reaches it only via pacing (app-discipline) or
EATT (= CoC bearers underneath). (GATT-sep-char 71ms is still < CoC-shared 290ms because GATT bulk
is 244B/1-PDU vs CoC 480B/2-seg, but it clearly does NOT isolate like the CoC channel.)
