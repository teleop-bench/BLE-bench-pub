# CoC credit bugs: two credit leaks + a Zephyr 0-initial-credit stall; the reconnect wedge root-caused (2026-10-06)

**Status: RESOLVED (bugs fixed in source; wedge root cause confirmed by a pre-registered test).** Follow-up to
`coc-duplex-credit-trace-20261006`. All runs: open `v4.4.2-16-gfee9fbc`, nRF54L15-DK pair, close range, CoC duplex
apps (`coc-duplex-central` / `coc-duplex-sink`), FSU off unless stated, `CONFIG_APP_CREDIT_TRACE=y` (500 µs samples of
each side's CoC TX state) where noted. Predictions for each step are in `PREDICTIONS.md` / `PREDICTIONS-oneway.md`,
written before the step ran (step results are appended before the next step's prediction).

## The bugs (all in our apps; the third is a Zephyr host bug our central triggered)
1. **`cen_avail` never reset** (`coc-duplex-central`): its count of outstanding uplink credits carried over to the next
   connection, so it over-granted credits after every peripheral-only reset. Same pattern (function-local `static`
   counters) in `coc-duplex-sink`, `coclat-sink`, `coclat2-sink`; those sinks reboot per rep in our procedures.
2. **Stale `rx.credits` sent as initial credits**: with `seg_recv` the host never resets a channel's `rx.credits`, and
   our apps reuse one static channel struct, so the previous connection's leftover credits (~60) went out as the next
   connection's initial credits. Together with (1), the sink's credit balance climbed 32 → 735 over a session.
3. **Zephyr host stall when a channel opens with 0 initial credits** (`subsys/bluetooth/host/l2cap.c`, v4.4.2-16):
   the acceptor calls `l2cap_chan_tx_give_credits(le_chan, 0)` on accept, which sets `BT_L2CAP_STATUS_OUT` with 0
   credits; its first send finds no credits and lowers the channel from the TX ready list without clearing
   `STATUS_OUT`; when credits arrive, `l2cap_chan_tx_give_credits` re-raises the channel only if `STATUS_OUT` was
   clear, so it never does → permanent TX stall on that channel (racy: only if the acceptor queues data before the
   first credits arrive). Our central granted its uplink window only after connect (0 initial credits). The leaked
   leftovers (2) gave every connection except the first after a central boot non-zero initial credits, which is
   exactly the documented "first CoC connection after central boot wedges the uplink" pattern and why `--cold` (both
   boards reset) stalled most reps.

Fixes (source): (1) counters reset when a fresh window is granted; (2) `atomic_set(&chan.rx.credits, 0)` before
connect/accept; (3) the central sends its 64-credit window in the connection request (as the sink always did).

## Runs
| step | what | n | result |
|---|---|---|---|
| A (`fix_test.py`, `results.jsonl`) | fix (1) only; queues central/sink 64/64, 64/20, 20/20 | 4 each | 57/117 (32.9% downlink), 92/92 (49.8%), 92/92 (49.8%); sink balance still grew ~60 per reconnect → found (2) |
| v2 (`v2_test.py`, `results-v2.jsonl`) | fixes (1)+(2), 64/64, peripheral-only | 4 | balance flat at 64; **reps 1–2: total uplink stall** (sink: data queued, 64 credits, controller empty, nothing sent: "hostheld" 100%); reps 3–4: 92/92 → found (3) |
| W (`w_test.py`, `results-w.jsonl`) | fixes (1)+(2)+(3), 64/64: 7.5 ms cold (both boards reset; measured connection = first after central boot), 7.5 ms peripheral-only, 25 ms cold | 6 + 4 + 4 | **14/14 no stall** (prediction: 0; historically `--cold` stalled most reps, 25 ms cold 10/12 on 09-06); every rep 1:1: 91–92/91–92 at 7.5 ms, 78/78 at 25 ms |
| one-way pilot (`ow_test.py`, `results-ow.jsonl`) | v3 central FSU off/on, sink with uplink off (batched credit returns), 7.5 ms | 4 each | 155.9 → 184.3 KB/s (**+18.2%**), all accepted; led to `coc-credit-policy-20261006` |

## What this changes
- **The open CoC duplex ~1:2 split needed the leaked credit surplus.** With all three fixes the recipe (64-deep queues
  on both sides) runs 1:1 (W). `coc-duplex-credit-trace-20261006` showed the credit returns queued behind the sink's
  64-deep controller FIFO; that mechanism is real, but the sink only filled that FIFO because the leak had given it
  hundreds of surplus credits. The published open CoC duplex numbers (split ~1:2, FSU +7.7 / +7.5 / +12.0%) were all
  measured with these bugs (`coc-duplex-fsu-matched-20261003`, `duplex-fsu-followups-20261003`, `replication-20261004`).
  Re-measured with the fixed apps in `coc-duplex-fsu-fixed-20261006`.
- **The reconnect wedge** (LESSONS, `nrf54l15` independent-rig 09-06) has a confirmed trigger: 0 initial credits on the
  uplink + the Zephyr host stall. It is not specific to 25 ms or to DLE (retracted triggers).
- W's and A's rates come from `coc-duplex-fsu.measure()` as it was then (downlink = median of the integer per-second
  line, ~1% high); later runs use the cumulative byte counter.

## Files
`PREDICTIONS.md`, `PREDICTIONS-oneway.md`, the four scripts, `results*.jsonl`, `run*.log`, `caps/` (both boards, incl.
`CTRACE` lines), `firmware/` (+ SHA256SUMS: `c64`/`s64`/`c20`/`s20` fix (1); `c64v2`/`s64v2` fixes (1)+(2); `c3-6`/`c3-20`
all fixes; `c1w-on`/`c1w-off`/`s1w` one-way pilot), `configs/`. The images predate the final source only in the fixes
listed per step.
