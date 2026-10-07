# On-air check of GATT echo duplex FSU with the nRF52 observer (2026-10-03)

**Status: RESOLVED (open stack, 7.5/15/25 ms).** An independent, controller-free confirmation of the
duplex FSU mechanism found in `gatt-duplex-fsu-model-20261003`: the observer counts packets per
connection event and measures the in-event gap directly on air.

## Result (21 accepted reps across `run1/` and `run2/`; `summary.txt`)
| interval | packets/event (off → on) | **full exchanges/event** | trailing cut-off packet (off / on) | in-event gap → tIFS (off / on) | complete events (off / on) |
|---|---|---|---|---|---|
| 7.5 ms | 6 → 7 | **3 → 3** | 1% / 100% | 174.4 → 150 µs / 76.4 → 52 µs | 332 / 359 |
| 15 ms | 13 → 14 | **6 → 6** | 100% / 100% | 150 / 52 µs | 107 / 105 |
| 25 ms | 21 → 23 | **10 → 11** | 100% / 100% | 150 / 52 µs | 96 / 124 |

Every accepted rep had a single packet count in 100% of its complete events. Three independent
measures now agree on full exchanges per event: the whole-exchange model, the open controller's
on-chip counter (`gatt-duplex-fsu-model-20261003`), and the air. FSU raises duplex throughput only
where the saved gap time fits one more whole exchange (25 ms), matching the measured +9.5–10%.

## A second observation: a trailing cut-off packet
In every event except 7.5 ms FSU-off, the event ends with one more packet that fails CRC at the
observer (FSU-on 7.5 ms: a 4th central packet after 3 full exchanges; 15 ms FSU-on: the peripheral's
7th reply). Acknowledgement bits (SN/NESN) toggle normally for all full exchanges. The observer decodes
the last packet of 7.5 ms FSU-off events cleanly, so this is not an observer limit. **Inferred, not
proven:** the open controller starts a packet that cannot finish before its event deadline and the radio
is cut off; receivers still see the declared length and then a bad CRC, so the packet is never
acknowledged and is resent next event. It costs airtime and energy, not throughput. It may be worth an
upstream note on `ll_sw_split` end-of-event scheduling.

## Method (`tools/onair-duplex.py`; REPRODUCE.md, "On-air duplex check (nRF52 observer)")
- Rig: nRF52832 DK observer (1050347760, `pca10040-radio-observer -DQ2=1 -DAIRTIME_MIN_TICKS=256`) +
  2× nRF54L15-DK running the GATT echo duplex images (`firmware/`, the same arms as
  `gatt-duplex-fsu-matched` / `gatt-duplex-fsu-model`, open `v4.4.2-16-gfee9fbc`).
- Per rep: reset the observer; bring up the link from boot; read the access address and CRC seed from
  the central's `Q2CONN` line (printed by the patched open controller) and wait for FSU (on: `spacing=52`;
  off: the 150 µs request); configure the observer on data channel 10 and capture 30 s (37-channel
  hopping, so the link visits channel 10 about once per 37 events).
- Analysis: split records into events on gaps > 4 ms; keep only complete events (every in-event gap ≤
  400 µs); full exchanges = packets / 2, excluding a trailing packet that fails CRC; gap = next ADDRESS −
  previous END (tIFS + ~24 µs preamble/access address at 2M). Roles by alternation (each event starts with
  the central). `--reanalyze` recomputes from the archived logs.
- Rejected/incomplete: run1 lost its first rep (the host idle-slept mid-capture; fixed with `caffeinate`)
  and its last (no `Q2CONN` line captured); run2 re-ran 7.5 and 25 ms, all 8 reps accepted.
- SDC links can't be observed this way: SDC does not expose the connection's access address.

Files: `run1/`, `run2/` (`results.jsonl` + `caps/` with observer, central and peripheral logs),
`firmware/` (+ SHA256SUMS), `configs/`, `summary.txt`. Local paths scrubbed with `tools/scrub-paths.py`.
