# Open CoC duplex ~1:2 split: root cause = credit returns queued behind the sink's deep controller TX queue (2026-10-06)

**Status: RESOLVED (causal, pre-registered).** Follow-up to `coc-duplex-split-diag-20261006`, which ruled out
buffers, app pools, SDU size and CPU but left the downlink pinned at ~57 KB/s. Predictions in `PREDICTIONS.md`
were written before each run (the interim observation and causal test were appended before the causal run).
7.5 ms, FSU off, open `v4.4.2-16-gfee9fbc`, same placement as `oneway-crossstack-20261005`; first connection after
each central flash discarded, peripheral-only resets (as `tools/coc-duplex-fsu.py`).

## 1. Trace: where the downlink waits (`results.jsonl`, `ctr_test.py`)
New diagnostic `CONFIG_APP_CREDIT_TRACE` (both apps, default off; default images byte-identical, verified) samples
each side's CoC TX state every 500 µs and prints a 1 s `CTRACE` line.

| arm | downlink / uplink KB/s | central: 0 credits | central: controller empty of its data | credit returns seen by central | sink: 0 credits | sink: controller buffers all full |
|---|---|---|---|---|---|---|
| duplex, traced (n=4) | 57 / 116.1–117.6 | 99.3% | **34.7–34.8%** | 5.9–6.0 /s | 0–5% | 95–100% |
| one-way control, sink uplink off (n=2) | 157 / 0 | 98.0% | **0%** | 16.0 /s | — | 0% |
| untraced reference, archived images (n=2) | 57 / 116.9–117.5 | | | | | |

The tracer does not move the rates. Zero credits alone is normal (the central runs out of credits in one-way too);
what differs is that in duplex the returns arrive late enough that the central's controller **drains and idles**
about a third of the time, which matches the ~1/3 of on-air events in which the central sends only empty packets
(`duplex-fsu-followups-20261003`). The sink never waits for credits, and its 64 controller ACL buffers stay full.
"Returns seen" counts credit increases between samples, so it is a rate, not the number of credits.
(The one-way reps carry an "uplink stalled" reject from `measure()` because the uplink is off by design.)

## 2. Causal test (`results-q.jsonl`, `q_test.py`): the sink's controller TX queue depth
On the open stack the controller's TX queue is `CONFIG_BT_BUF_ACL_TX_COUNT` deep (64 in the sink recipe). The sink's
credit-return PDUs enter that FIFO behind its buffered uplink packets. Variable: sink `BT_BUF_ACL_TX_COUNT`
64 → 20 → 8 (`BT_ATT_TX_COUNT` pinned at 64, so the matched-pair check passes with only that symbol different);
order 64, 20, 8, 8, 20, 64, 64, 20, 8, 8, 20, 64; n=4 each, 12/12 accepted.

| sink controller TX queue | downlink KB/s | uplink KB/s | aggregate | downlink share | central controller empty | credit returns seen |
|---|---|---|---|---|---|---|
| 64 (recipe) | 56.8 | 116.4 | 173.2 (168.3–174.9) | 32.8% | 34.8% | 5.9 /s |
| 20 | 91.8 | 92.5 | 184.2 (182.7–184.8) | **49.8%** | 0% | 9.4 /s |
| 8 | 92.0 | 92.6 | 184.6 (184.3–184.9) | **49.8%** | 0% | 9.5 /s |

**Prediction held:** the split moves from ~1:2 to 1:1 and the central's idle time disappears. The pre-registered
"monotonic" detail did not: the effect is already complete at 20, and 8 is the same. The aggregate also rises
~6% (173 → 184 KB/s), because the downlink no longer idles.

## Reading
- **Root cause:** credit-return head-of-line blocking. The sink's L2CAP credit PDUs wait in its controller's FIFO
  behind up to 64 queued uplink packets (≈ 130 ms at ~480 packets/s; an estimate from the rates, not timed
  directly), so the central exhausts its window, drains, and idles. Reducing the queue to 20 removes the wait.
- Why the host-side pools didn't matter (`coc-duplex-split-diag-20261006` tests 2 and 5): the reordering problem is in
  the controller FIFO, not in the host's queues. Inferred: the 4.4 host schedules channels round-robin, so a
  signaling PDU can pass queued data in the host, but not once both are in the controller.
- **SDC's 1:1 is consistent with this** (its controller holds `SDC_TX_PACKET_COUNT=20` packets) but was not tested.
- **Consequence for the published CoC duplex FSU result:** open CoC duplex +7.7 / +7.5 / +12.0% (7.5 / 15 / 25 ms,
  `coc-duplex-fsu-matched-20261003`) was measured with the 64-deep sink queue, and its mechanism (one-way events
  from the uneven split) comes from that queue. With a 20-deep queue the link is balanced, so the gain may revert
  toward the GATT duplex pattern (≈0% at 7.5/15 ms). **Not measured**; the published numbers stand for the recipe
  as configured.
- Not explained here: why `--cold` (both boards reset) gave an even split in `duplex-fsu-followups-20261003`.

## Pitfalls found
- The central app's receive-credit counter (`cen_avail`, `coc-duplex-central`) is static and never reset on
  reconnect, so every peripheral-only reset grants the sink extra credits (its mean balance climbs 32 → 735 over a
  session). Harmless here (the sink is never credit-limited, and r1 vs r12 at 64 read 56/112 vs 57/118), but it
  means the sink's credit window is not the recipe's 64 after the first rep. Not fixed (the fix would change the
  default image the archived campaigns were built from).
- A queued follow-on run waited on `pgrep -f ctr_test.py` from inside an `sh -c` whose own command line contained
  `ctr_test.py`; it matched itself and never started (~50 min lost). Match the interpreter too (`pgrep -f
  "python3.*ctr_test.py"`) or wait on the PID.

## Files
`PREDICTIONS.md`, `ctr_test.py` (trace run), `q_test.py` (causal run), `results.jsonl`, `results-q.jsonl`,
`run.log`, `run-q.log`, `caps/` (both boards' logs incl. every `CTRACE` line), `firmware/` (+ SHA256SUMS: `c-tr`/
`s-tr` traced, `s-tr-1way` uplink off, `s-tr-q20`/`s-tr-q8` shallow queue, `c-baseline`/`sink` untraced),
`configs/`. New options: `CONFIG_APP_CREDIT_TRACE` (+ `_US`) in both apps, `CONFIG_APP_UPLINK` in the sink.
