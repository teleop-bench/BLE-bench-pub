# Stop-signal latency under load, FSU on vs off (matched arms, open stack, 2026-10-03)

**Status: RESOLVED (open stack, GATT, 7.5 ms naive + paced and 25 ms naive; one session, close range).**
First FSU on/off comparison of the safety link's stop-signal latency under concurrent bulk load. A
replication on 2026-10-04 (boards further apart) is archived separately.

## Result 1: standard ramp, n=4 per arm (`latency/` + `latency-b/`, pooled; `summary.txt`)
Mean over reps of each stage's on-device percentiles (thousands of pings per stage). Bulk = rate the
peripheral actually received; both arms met every target. No ping timed out in any rep.

| config | load (KB/s) | p50 off → on (ms) | p99 off → on | p99.9 off → on |
|---|---|---|---|---|
| 7.5 ms, naive | 0 | 11 → 11 | 15.0 → 15.0 | 20.8 → 20.8 |
| | 25 | 11 → 11 | **33.8 → 28.0** | 38.5 → 32.5 |
| | 75 | 11 → 11 | **40.5 → 31.2** | 48.2 → 40.0 |
| | 100 | 11 → 11 | 37.2 → 33.0 | 44.2 → 44.8 |
| | 150 | **32.5 → 26.0** | **39.5 → 31.5** | 46.2 → 36.8 |
| 7.5 ms, paced | 0 | 11 → 11 | 11.0 → 11.0 | 13.0 → 13.0 |
| | 25 | 11 → 11 | 19.0 → 15.8 | 22.8 → 19.0 |
| | 100 | **19.0 → 14.8** | 20.8 → 18.8 | 24.5 → 22.0 |
| | 150 | **19.0 → 15.0** | **22.0 → 19.0** | 27.5 → 22.2 |
| 25 ms, naive | 0 | 46 → 46 | 46.2 → 46.0 | 64.8 → 58.5 |
| | 50 | 46 → 46 | 53.8 → 48.0 | 67.2 → 49.2 |
| | 150 | **30.0 → 25.0** | **57.0 → 50.2** | 67.5 → 68.2 |

Reading (measured): under load FSU lowers the stop-signal p99 by ~2–9 ms (~10–25%) in all three
configurations, and the median by 4–6.5 ms once load moves it; idle latency is unchanged; p99.9 mostly
follows (two cells flat). Pacing remains the larger lever: paced FSU-off (p99 22.0 ms at 150 KB/s) beats
naive FSU-on (31.5 ms); paced + FSU is the lowest measured (p99 15.8–19.0 ms under load at 7.5 ms).

**Report percentiles, not the `>30 ms` share.** Loaded RTTs at 7.5 ms sit around 30 ms, so the few-ms
FSU shift moves the `>30 ms` share from 95% to 1% (naive, 150 KB/s) while p99 moves 39.5 → 31.5 ms.

## Result 2: high-load ramp, 0/140–190 KB/s, n=2 per arm (`latency-high/`, `CONFIG_APP_LOAD_RAMP_HIGH=y`)
| config | ceiling: bulk delivered off / on (KB/s) | p50 off → on at 160–190 KB/s | p99 off → on at 160–190 KB/s |
|---|---|---|---|
| 7.5 ms, naive | ~150 / ~180 | 33 → 26 | 38–40.5 → 30.5–34 |
| 7.5 ms, paced | ~147 / ~170 | 19 → 15 | 22–25.5 → 19–21 |
| 25 ms, naive | ~158 / ≥185 | 30 → 25 | 48.5–50.5 → 44–48 |

- **Measured:** above its ceiling the FSU-off arm's tail **plateaus** (the 10-buffer queue bounds the
  backlog); it does not blow up. The FSU-on arm, also saturated at the top stages and delivering ~20% more
  bulk, keeps a lower median (−5 to −7 ms) and p99. So the FSU latency gain is **not** just spare capacity
  at a given load: with FSU the link carries more bulk *and* keeps a lower stop-signal tail.
- **Inferred, not measured:** each queued packet drains faster with the shorter gap, so a ping waits less
  behind the queue (the naive 7.5 ms median drops 33 → 26 ms, about one 7.5 ms interval; the paced median
  drops 19 → 15 ms, less than one). The per-event mechanism has not been checked on air.

## Rejected/aborted
- `latency-gdeep-censored/`: the first attempt added `gdeep.conf` (64 ACL TX buffers, `configs/`). Its
  FSU-off rep had p99 ≈ 195–200 ms and 6–39 timeouts per stage from 50 KB/s up: the tail sat on the 200 ms
  ping timeout (censored), so no FSU difference could show. Stopped after one complete rep; re-run with the
  10-buffer queue of the accepted hardening result. Kept as evidence for the LESSONS entry.
- `latency-b/` rep `lat6polite_on_r3` was first rejected by a parser bug (a reset glued a fragment of the
  previous boot's `interval=7500` line onto the boot banner); `--summarize` re-measures it with the fixed
  parser and accepts it. `results.jsonl` keeps the original verdict.

## Design (`tools/latency-fsu.py`; REPRODUCE.md, "Latency under load with FSU")
- Central `z54-lat-central` `loadramp.conf;fsu-open.conf` + `hh-fsu52.conf` (on) / `hh-fsu150.conf` (off),
  `CONFIG_APP_CONN_INT_UNITS` 6 or 20, paced arm `+CONFIG_APP_LOAD_POLITE=y`; matched-pair gate MATCHED for
  every config. Peripheral `z54-lat-periph` same overlays + `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`.
- Order per config: off, on, on, off (one ABBA block per run; two runs pooled → n=4 per arm). Each rep:
  flash the central, open the capture, reset both boards, capture 450 s from boot.
- Gates per rep: FSU-on `FSU: updated … spacing=52`; FSU-off `FSU: request [150..150] rc=0` and no reduced
  spacing; interval = configured; all 7 `PCTL` stages present.

Files: `latency/`, `latency-b/`, `latency-high/`, `latency-gdeep-censored/` (`results.jsonl` + `caps/`),
`firmware-b-lat2/` (standard ramp) and `firmware-b-lathigh/` (high-load), each + SHA256SUMS, matching
`configs-*`, `summary.txt`, `chain4.sh`/`chain5.sh` (exact run order). Local paths scrubbed with
`tools/scrub-paths.py`.

Rig: 2× nRF54L15-DK (central 1057719509, peripheral 1057794857), close range, open `v4.4.2-16-gfee9fbc`.
