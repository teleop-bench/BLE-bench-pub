# Why FSU lowers stop-signal latency, and SDC latency under load (2026-10-04)

**Status: RESOLVED (one session, boards at the wider `replication-20261004` placement).** Two results:
(1) the mechanism behind FSU's lower stop-signal tail, measured with the open controller's per-event
transaction counter and pre-registered in `PREDICTIONS.md`; (2) the first matched FSU on/off latency-under-load
run on Nordic SDC (previously one provisional unpaced cycle).

## 1. Mechanism: full events hold 5 bulk exchanges without FSU, 6 with it (open Zephyr, 7.5 ms)
`diag/` (standard ramp, naive + paced) and `diag-high/` (140–190 KB/s ramp, naive); n=2 per arm; counter =
`CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG` (`RPT pe:` every 2 s, summed per load stage; "full" = largest event size
seen in ≥ 5% of events).

| configuration | full event, FSU off → on | transactions/event at 25 KB/s (off / on) | p99 at 25 / 100 KB/s, off → on (ms) | bulk ceiling off → on (KB/s) |
|---|---|---|---|---|
| naive | **5 → 6** at every loaded stage | 1.64 / 1.66 | 47.5 → 38.0 / 55.0 → 43.5 | ~126 → 149 at the 150 stage |
| paced | **5 → 6** at every loaded stage | 1.66 / 1.70 | 26.5 → 23.0 / 31.5 → 25.5 | ~136 → 149 at the 150 stage |
| naive, high ramp | **5 → 6** at every stage 140–190 | — | 48–50 → 41–43 at 140–190 | **~139 → ~166** (+19%) |

- **Prediction held** (written before the run): a full bulk exchange is a 251-byte packet + an empty reply +
  two gaps = 1392 µs without FSU, 1196 µs with it, so 5 vs 6 fit in a 7.5 ms event. The counter shows exactly
  that, in every loaded stage of all three configurations, and the ceiling ratio (~1.19) matches 6/5.
- **Why it helps below the ceiling:** at 25 KB/s both arms average the same ~1.65 transactions per event
  (same bulk delivered), but the load generator sends each second's quota as a burst. During the burst,
  events are full — 5 exchanges without FSU, 6 with it — so the backlog drains ~20% faster and the
  stop-signal ping waits behind fewer events. Above the ceiling both arms run full events continuously and
  the FSU arm still drains 6 per event, which is why its tail stays lower at saturation.
- Medians at 125–190 KB/s drop from 34–37 ms to 26–28 ms (naive) and 19 → 15 ms (paced).

## 2. SDC latency under load, FSU off → on (`sdc/`, n=2 per arm; SDC negotiated a 70 µs gap)
| configuration | idle p99 | p99 at 50 KB/s | p99 at 100 KB/s | median at 150 KB/s | bulk ceiling (150 stage) |
|---|---|---|---|---|---|
| 7.5 ms naive | 19 → 19 | 52.0 → 43.0 | 60.0 → 47.5 | 41 → 34 | 114 → 136 |
| 7.5 ms paced | 19 → 19 | 34.0 → 28.0 | 37.0 → 36.0 | 23 → 19 | 112 → 134 |
| 25 ms naive | 71 → 71 | 76.5 → 71.0 | 78.5 → 73.5 | 33 → 29 | 123 → 145 |

- FSU behaves on SDC as on the open stack: lower loaded tail (p99 −1 to −20 ms at most loaded stages, largest at
  the 150 stage where FSU-off is already past its ceiling; one 25 ms stage +1.5 ms, within missed-event noise), unchanged idle latency, no timeouts, ~+19% bulk ceiling.
- At 25 ms the p99 sits at ~71–80 ms in both arms (missed connection events at this placement, as for the
  open stack in `replication-20261004`); the median still drops ~4 ms under load.
- **SDC vs open (not a controller verdict):** at this placement SDC's loaded tail is higher than the open
  stack's (e.g. paced p99 at 100 KB/s ~37 ms vs ~28 ms in `replication-20261004`) and its 7.5 ms bulk ceiling
  lower (~113 vs ~140 KB/s). The SDC recipe adds a 10-packet controller TX queue
  (`CONFIG_BT_CTLR_SDC_TX_PACKET_COUNT=10`, the documented throughput tuning) on top of the host's 10 ACL
  buffers, so part of the tail difference is queue depth. Different sessions; compare deltas.

## Design (`tools/latency-fsu.py --diag`, `--high`, `--stack sdc`; REPRODUCE.md "Latency under load with FSU")
- Open: `loadramp.conf;fsu-open.conf` + `hh-fsu52.conf` / `hh-fsu150.conf`, `+CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y`.
- SDC (NCS v3.4.0): central `sdc-sel.conf;loadramp.conf;sdc-llbuf.conf` + `hh-sdc-fsu.conf` / `hh-sdc-nofsu.conf`;
  peripheral `sdc-sel.conf;loadramp.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf`, auto-update off. The SDC
  FSU request is issued ~12 s after connecting, inside the idle stage.
- Matched-pair design gate MATCHED for every configuration; per-rep gates (FSU on: reduced spacing — 52 µs
  open, < 150 µs SDC; FSU off: the 150 µs request, no reduced spacing; interval; 7 stages). 36/36 reps accepted.
- The first SDC attempt (`chain.sh` step L1) crashed before building (tool bug: the shell helper rejected
  the `cwd` argument); fixed and re-run as `chain2.sh`.

Files: `PREDICTIONS.md`, `chain.sh`, `chain2.sh`, `diag/`, `diag-high/`, `sdc/` (`results.jsonl` + `caps/`),
`firmware-b-*/` (+ SHA256SUMS; the SDC images are listed in THIRD-PARTY-NOTICES.md), `configs-b-*/`,
`summary.txt`. Local paths scrubbed with `tools/scrub-paths.py`.

Rig: 2× nRF54L15-DK (central 1057719509, peripheral 1057794857); open `v4.4.2-16-gfee9fbc`, SDC NCS v3.4.0.
