# One-way CoC "FSU ≈ 0% at 7.5 ms" is the sink's per-segment credit returns, not CoC (2026-10-06)

**Status: RESOLVED (causal, pre-registered; retracts the "CoC FSU ≈ 0% at 7.5 ms on both stacks" finding as a
property of CoC).** Raised by a code audit: the one-way recipe sink (`apps/coc/coc-sink`) calls
`bt_l2cap_chan_give_credits(chan, 1)` for every received segment, and the host sends one L2CAP credit PDU per call,
so most of the sink's replies are 12-byte credit PDUs (~92 µs at 2M) instead of empty packets (~44 µs). At 7.5 ms with
52 µs gaps that costs the 6th exchange per connection event (5 → 5), while GATT goes 5 → 6.

## Design (`credit_test.py`, `design-gate.txt`, `PREDICTIONS.md`)
- Images built exactly as `tools/oneway-fsu.py` builds the one-way CoC recipe ("max" profile, 7.5 ms, 480 B SDUs) for
  both stacks; the sink in two variants: recipe (per-segment credits) and recipe + `CONFIG_APP_CREDIT_BATCH=y` (one
  credit PDU per ~41 segments: refill when fewer than 24 of 64 remain).
- Gates: centrals on/off matched; sinks differ only in `CONFIG_APP_CREDIT_BATCH`; **both stacks' default sinks are
  byte-identical to the images behind the published result** (`oneway-crossstack-20261005/firmware/max-coc-*-sink.hex`).
- 2 stacks × 2 sinks × FSU off/on, n=4 per cell, interleaved (every round visits all 8 cells; even rounds reversed);
  per rep: flash if changed, open capture, reset both boards, 32 s; `oneway-fsu.measure()` gates (FSU held, interval,
  window after FSU onset). 32/32 accepted. KB/s from the sink's cumulative byte counter over the measurement window.

## Result (`results.jsonl`)
| stack | sink credit returns | FSU off | FSU on | gain |
|---|---|---|---|---|
| Zephyr | per segment (recipe) | 155.9 | 155.8 | −0.1% |
| Zephyr | batched | 155.9 | 186.7 | **+19.8%** |
| SDC | per segment (recipe) | 124.8 | 124.9 | +0.1% |
| SDC | batched | 124.5 | 155.9 | **+25.2%** |

(Per-rep values: Zephyr batched on 186.3–187.1; SDC batched on 155.6–156.0; all other cells within ±0.9 of their mean.)

## Reading
- The ≈0% CoC FSU gain at 7.5 ms reproduces exactly with the recipe sink and disappears when only the credit policy
  changes: with batched returns CoC gains like GATT on both stacks (GATT, `oneway-crossstack-20261005`: +20.2% Zephyr,
  +24.4% SDC). The prediction held (Zephyr +18–20% predicted; SDC "clearly > +10%").
- FSU off is unaffected by the policy (155.9 / 124.5–124.8): at 150 µs gaps the longer replies still fit the same
  number of exchanges, which is why the effect only appears with FSU.
- Zephyr − SDC stays ~31 KB/s at 7.5 ms with either policy.
- The published CoC gains at 15–50 ms used the per-segment sink too; the audit's arithmetic says they may also be
  reduced. Re-measured with batched returns in `oneway-coc-batched-20261006`.
- Practical: a CoC receiver that returns one credit per segment taxes every reply; batch credit returns.

Files: `PREDICTIONS.md`, `credit_test.py`, `design-gate.txt`, `results.jsonl`, `run.log`, `caps/`, `firmware/`
(+ SHA256SUMS; SDC images listed in THIRD-PARTY-NOTICES.md), `configs/`. New build option: `coc-sink`
`CONFIG_APP_CREDIT_BATCH` (default off, image unchanged — verified byte-identical).
