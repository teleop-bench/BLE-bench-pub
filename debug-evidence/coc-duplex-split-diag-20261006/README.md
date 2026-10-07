# Why open-stack CoC duplex splits ~1:2: five hypotheses tested, none confirmed (2026-10-06)

**Status: OPEN (narrowed, not root-caused).** Open Zephyr CoC duplex at 7.5 ms runs ~57 KB/s downlink (central →
sink) vs ~117 KB/s uplink, while SDC splits ~1:1 with the same apps. Five single-variable tests, each pre-registered
in `PREDICTIONS.md` before running, each matched-pair verified (only the variable under test differs), each n=4
vs n=4 alternating (ABBA×2; test 4 is a single observation rep), FSU off, same procedure as `tools/coc-duplex-fsu.py`
(first connection after a central flash discarded; peripheral-only resets).

| # | Hypothesis | Variable | Downlink share | Verdict |
|---|---|---|---|---|
| 1 | Central receive starvation | central controller/host RX buffers 1/1 → 8/8 (sink's values) | 32.8% → 32.8% | falsified |
| 2 | Central's shallow app send pool | central SDU pool 16 → 64 | 32.7% → 32.7% | falsified |
| 3 | SDU segmentation (480 B = 2 LL packets) | central SDU 480 → 244 B (sink's size, 1 packet) | 32.7% → 33.1% | falsified |
| 4 | CPU/thread starvation | thread analyzer on both boards | central idle 87%, sink 85% | falsified* |
| 5 | Sink's credit returns stuck behind its deep uplink pool | sink SDU pool 64 → 16 (as its own code comment intends) | 32.8% → 32.9% | falsified |

\* ISR time may be attributed to the idle thread; no asymmetry between the boards either way.

## What is now known
- **The downlink is a hard cap: 57–58 KB/s in every one of 33 reps (32 A/B + the analyzer rep)**, whatever was changed, i.e. ~2 LL data packets
  per 7.5 ms event on average; on air (`duplex-fsu-followups-20261003`) the central has data in ~2 of 3 events and
  sends only empty packets in the third. Alone (one-way) the same central sends ~150 KB/s.
- **Not** receive buffering, app pool depth on either side, SDU size/segmentation, or CPU load.
- **Correction:** credit starvation was earlier called "ruled out" from the central's `eagain=0`. That was wrong:
  in Zephyr 4.4 `bt_l2cap_chan_send` queues an SDU when credits run out instead of returning `-EAGAIN`, so that
  counter can't see credit waits. The central's actual credit count has never been logged.
- The sink's pool comment says 16 ("DUPLEX FIX … 16 << 64-credit window") but the code used 64; test 5 shows the
  mismatch has no effect. `CONFIG_APP_TX_POOL_COUNT` now exposes it (default 64, image unchanged).

## Next measurement (not run)
Log the central's available TX credits (`le_chan.tx.credits`) and the sink's credit-return events with timestamps,
alongside the per-event controller counter. If the central sits at 0 credits during the one-way events, the cap is
credit pacing (then: why the returns arrive at that rate); if it holds credits while its LL queue is empty, the cap
is in the host→controller handoff of the open stack.

## Files
`PREDICTIONS.md` (pre-registered per test, with results appended), `split_test.py`, `pool_test.py`, `sdu_test.py`,
`ta_test.py`, `sink_test.py`, `results*.jsonl` (test 4's measure step crashed on the analyzer's
untimestamped lines; `tools/coc-duplex-fsu.py` now skips them, and `results-ta.jsonl` was recomputed from `caps/ta-*`: 57 / 117.9 KB/s), `run*.log`, `caps/`, `firmware/` (+ SHA256SUMS), `configs/`. New build options (defaults leave
the images byte-identical, verified): `coc-duplex-central` `CONFIG_APP_TX_POOL_COUNT`, `CONFIG_APP_DUPLEX_SDU_SIZE`;
`coc-duplex-sink` `CONFIG_APP_TX_POOL_COUNT`. Placement as `oneway-crossstack-20261005`; open `v4.4.2-16-gfee9fbc`.
