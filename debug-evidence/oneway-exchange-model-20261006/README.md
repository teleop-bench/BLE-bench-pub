# One-way throughput explained by whole exchanges per event; SDC budgets for a maximum-length reply (2026-10-06)

**Status: RESOLVED as a model fit (derived, no new hardware runs; inferred, not observed inside SDC).** Explains three
previously open items with one rule per controller: SDC's ~30 KB/s deficit at 7.5 ms, SDC GATT (142) vs CoC (156)
at 15 ms without FSU, and why the per-segment credit sink costs SDC throughput at some intervals and not others.

## Data (`fitdata.json`)
Mean KB/s of every one-way cell (5 intervals × FSU off/on × stack): GATT from `oneway-crossstack-20261005`; CoC with
per-segment credits re-computed from that dir's archived captures with the cumulative-byte-counter method; CoC with
batched credits from `oneway-coc-batched-20261006`. 60 cells.

## Model (`fit_model.py`, `fit_rulec.py`, `fit_check.py`; outputs `*.out`)
A connection event carries whole exchanges (central data packet + peripheral reply + 2 gaps); rate = exchanges × SDU
bytes per packet / interval. 2M airtime (11 + payload bytes) × 4 µs: CoC data packets alternate 251 / 239 B (480 B SDUs
in 247 B segments, 1048 / 1000 µs, 240 B SDU per packet), GATT 251 B (1048 µs, 244 B per packet); replies are empty
(44 µs) or a 12-byte credit packet (92 µs, per-segment sink). Gaps: 150 µs; FSU 52 µs (Zephyr), 65 µs (SDC CoC), 70 µs
(SDC GATT). Three rules for when the central starts another exchange, each with one fitted parameter:
- **A**: if the exchange ends at least M before the next event;
- **B**: if a maximum-length pair (2 × 1048 µs + 2 gaps) would end at least m before it;
- **C**: if its own data packet + a **maximum-length reply** + 2 gaps would end at least m before it.

## Result
| controller | best rule | fitted parameter | one-way cells within 2.5% | independent check |
|---|---|---|---|---|
| Zephyr (open) | A (constant reserve) | M = 182–360 µs | 28 / 30 (CoC 19/20 fitted, GATT 9/10 out-of-sample) | the duplex model's 250–312 µs (fitted separately on duplex data) |
| SDC | **C (max-length reply)** | m = 292–316 µs | 29 / 30 | **SDC duplex: exchange counts right in all 12 cells** (GATT echo + CoC, 7.5/15/25 ms, off/on); rule A with its one-way fit (M ≈ 1.29 ms) gets 9 of 12 wrong |

The two Zephyr misses: GATT 25 ms FSU off (the cell with the known radio-dip rep; −0.3% without it) and CoC batched
50 ms FSU on (−2.6%). The SDC miss: CoC per-segment 15 ms FSU on, a 14 µs knife edge (rule C allows an 11th exchange
with 14 µs to spare; the board ran 10). Measured rates sit 0–2.7% below the model, more at long intervals, consistent
with occasional lost packets. Zephyr under rule C fits only 15/30; SDC under rule A fits its one-way data (M ≈ 1.29 ms)
but not its duplex data.

## Reading
- **SDC appears to start an exchange only if a maximum-length reply would still fit** (it cannot know the reply length
  in advance), keeping ~0.3 ms in hand; **Zephyr keeps a ~0.27 ms margin for the exchange as it actually runs.**
  In one-way traffic the replies are short, so SDC leaves roughly one maximum-length packet (~1 ms) unused at the end
  of every event: 4 exchanges vs Zephyr's 5 at 7.5 ms, which is the ~30 KB/s gap on both transports. The cost is
  amortized at long intervals, where the stacks converge. In duplex the replies really are full length, so the two
  rules coincide, matching the measured parity between the stacks at 7.5 ms (GATT echo 187 vs 187).
- SDC GATT 15 ms without FSU (142) vs CoC (156): GATT's 1048 µs packets fit 9 exchanges where CoC's average 1024 µs fit 10.
- The per-segment credit sink's interval-dependent cost on SDC: the 48 µs longer replies cost an exchange only where an
  event was close to full (e.g. 15 ms: 10 → 9 without FSU; 25 ms: 17 → 16).
- Consistent with Nordic's documentation that SDC extends an event "one packet pair at a time", which does not say what
  length it assumes for the pair. Inferred from rates, not observed: SDC's packets can't be followed by our observer.
- Zephyr's not budgeting for the reply fits the on-air observation that its events usually end with a cut-off packet.

Files: `fitdata.json`, `fit_model.py` (rules A/B, per-cell table), `fit_rulec.py` (rule C + SDC duplex check),
`fit_check.py` (misses; Zephyr under rule C), `*.out`.
