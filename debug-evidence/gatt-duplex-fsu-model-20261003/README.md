# GATT echo duplex FSU — SDC, 15 ms, and the whole-exchange model (2026-10-03)

**Status: RESOLVED.** Extends `gatt-duplex-fsu-matched-20261003` (open stack, 7.5/25 ms) to the
SoftDevice Controller and to 15 ms, and confirms the mechanism with the controller's own
per-event counter. All runs used `tools/gatt-duplex-fsu.py` (matched arms, held FSU, post-onset
window, ABBA n=4 per arm per interval); see REPRODUCE.md, *GATT duplex FSU (matched arms)*.

## Results (aggregate = echo received + blast received, KB/s; Welch 95% interval)
| run | interval | FSU off (n) | FSU on (n) | FSU gain | rejected |
|---|---|---|---|---|---|
| `sdc-7p5-25ms/` (SDC, 70 µs) | 7.5 ms | 187.0 (4) | 187.6 (4) | +0.3% ± 0.9% | 0 |
| | 25 ms | 185.9 (4) | 206.0 (4) | **+10.8% ± 0.2%**, strict non-overlap | 0 |
| `sdc-15ms/` (SDC) | 15 ms | 187.5 (4) | 187.1 (4) | −0.2% ± 1.5% | 0 |
| `open-15ms/` (open, 52 µs) | 15 ms | 186.0 (2) | 186.0 (4) | +0.0% | 2 (see below) |
| `open-diag-7p5-15-25ms/` (open, event-fill counter on) | 7.5 ms | 187.6 (4) | 187.1 (4) | −0.3% ± 0.9% | 0 |
| | 15 ms | 186.5 (4) | 186.5 (4) | +0.0% ± 0.8% | 0 |
| | 25 ms | 185.8 (4) | 204.5 (4) | **+10.1% ± 0.8%**, strict non-overlap | 0 |

Open stack at 7.5/25 ms without the counter: `gatt-duplex-fsu-matched-20261003` (+0.3% / +9.5%).

## Mechanism: whole exchanges per connection event (measured)
In echo duplex one exchange = a full central packet + a full peripheral packet, each followed by
the inter-frame gap. At 2M a 251-byte PDU is ~1048 µs on air, so an exchange takes
2×1048 + 2×150 = **2396 µs** without FSU, **2200 µs** at 52 µs (open), **2236 µs** at 70 µs (SDC).
FSU helps only if the saved time fits one more whole exchange into the event:

| interval | predicted exchanges/event (off → on) | **measured** (controller counter, mode; mean) | measured gain |
|---|---|---|---|
| 7.5 ms | 3 → 3 | **3 → 3** (2.99; 2.99) | none |
| 15 ms | 6 → 6 | **6 → 6** (5.94; 5.95) | none |
| 25 ms | 10 → 11 | **10 → 11** (9.87; 10.84) | +10% |

The counter is the patched open controller's `trx_cnt` histogram
(`CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG`), printed by `z54-lat-central/src/eventfill.c` in diag builds
only. SDC has no such counter; its throughput pattern matches the same prediction (70 µs: 25000/2236
= 11.2 → 11 exchanges). The duplex FSU gain is therefore **not monotonic in interval**: it appears
where the saved time crosses a whole exchange (consistent with the earlier 08-24 interval pattern:
15 ms +1%, 25 ms +11.9%, 37.5 ms +6.4%, 50 ms +10.8%, from a run with a confounded off-arm).

## Notes
- `open-15ms/`: two FSU-off reps were rejected by the gates because the peripheral's serial capture
  stopped early (USB on board 1057794857; the radio link stayed up, the central kept receiving echoes).
  The diag run measured open 15 ms again at n=4+4 with no rejections.
- The event-fill counter barely affects GATT throughput (diag-run gains match the non-diag runs).
- Scope: GATT echo duplex, close range, open `v4.4.2-16-gfee9fbc` and SDC (NCS v3.4.0), one session.
  CoC duplex with independent traffic in each direction is not covered (reconnect wedge).

## Files (per run dir)
`results.jsonl`, `caps/` (raw logs), `firmware/` (hexes + SHA256SUMS), `configs/` (resolved
`.config`); `sdc-7p5-25ms/results-smoke.jsonl` is the pre-run smoke. SDC images are covered by
THIRD-PARTY-NOTICES.md. Local paths scrubbed with `tools/scrub-paths.py`.
