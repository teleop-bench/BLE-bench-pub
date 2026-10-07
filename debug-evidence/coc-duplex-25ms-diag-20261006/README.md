# Zephyr CoC duplex at 25 ms, FSU off: credit-loop starvation from the 64-deep controller queue (2026-10-06)

**Status: RESOLVED (causal, pre-registered).** Explains the provisional cell in `coc-duplex-fsu-fixed-20261006`
(Zephyr 25 ms FSU off 154.5 KB/s vs SDC 186.3 and Zephyr's own ~183 at 7.5 / 15 ms; gain +23.8%). Each step's
prediction is in `PREDICTIONS.md`, written before that step ran. Fixed apps (credit bugs of `coc-credit-fixes-20261006`
corrected), open `v4.4.2-16-gfee9fbc`, close range.

| step | what | result |
|---|---|---|
| A (`z25_trace.py`, `results.jsonl`) | `CONFIG_APP_CREDIT_TRACE` both sides, 25 ms, off/on, n=2 | identical in both arms: both sides at 0 credits ~100% of samples, "controller not empty", no host-held data |
| B (`onair/`, `onair-*.txt`) | nRF52 observer, 15 ms (control) and 25 ms, off/on, n=2 | most common FSU-off 25 ms event = 10 full exchanges (on: 11; 15 ms: 6/6); 0 retransmissions; but the events are **bimodal**: ~40 full + ~13 ending after 2–4 exchanges with MD = 0 on both sides (`onair-histogram.txt`); FSU on: 9–11 throughout |
| C (`ev_test.py`, `results-ev.jsonl`, `caps/ev25-*`) | open controller's event counter (`CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y`), n=2 | ~400 events per 10 s in both arms (no missing events: the prediction failed), mean exchanges per event 8.2–8.3 (off) vs 10.2–10.4 (on): ratio 0.80 = the throughput ratio |
| D (`q20_test.py`, `results-q20.jsonl`) | 20-deep controller TX queues on both sides (`BT_BUF_ACL_TX_COUNT=20`, `BT_ATT_TX_COUNT=64`), n=2 | **FSU off 184.5 / 184.8, on 204.0 / 205.1: +10.8%**, = SDC (+10.2%) and the model (~+10%) |

## Reading
- In ~20% of FSU-off 25 ms events both radio queues ran dry after a few exchanges: each side's L2CAP credit returns wait
  in its 64-deep controller FIFO behind its own data, so both sides periodically exhaust their credited data together.
  With 20-deep queues (SDC's controller holds 20) the starvation disappears and Zephyr matches SDC and the model.
  Why it bites at 25 ms FSU off and not at 7.5 / 15 ms or 25 ms FSU on is not established (7.5 ms was insensitive to
  the queue depth in `coc-credit-fixes-20261006` step A).
- **Instrument lesson:** step A's "controller not empty" counts buffers the host has not yet seen completed, which
  includes packets already sent but not yet acknowledged, so it could not see the radio queue going dry. Step B's
  per-event *mode* (10) hid the short events too; the event counter's *mean* and the full histogram showed them.
- The published Zephyr 25 ms +23.8% (`coc-duplex-fsu-fixed-20261006`) is therefore a queue-depth artifact; the full
  Zephyr CoC duplex re-run with 20-deep queues is `coc-duplex-fsu-q20-20261006`.

Files: `PREDICTIONS.md`, scripts (`z25_trace.py`, `ev_test.py`, `q20_test.py`, `retx.py`, `evrate.py`, `hist.py`),
`results*.jsonl`, `run-*.log`, `caps/`, `onair/` (observer captures + results), `onair-*.txt`, `firmware/` (+ SHA256SUMS:
traced, event-counter and 20-deep images; `observer-2m.hex` = the archived observer image), `configs/`.
