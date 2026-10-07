# Pre-registered (2026-10-06, before the run): where does the open CoC duplex downlink wait?
Follow-up to coc-duplex-split-diag-20261006 (downlink pinned ~57 KB/s, uplink ~117; buffers, pools, SDU size, CPU
ruled out). New diagnostic CONFIG_APP_CREDIT_TRACE (default off, default images byte-identical) samples each side's
CoC TX state every 500 us: nofeed (no SDU waiting in L2CAP) / cr0 (waiting, 0 credits) / ctrlfull (waiting, credits,
all controller ACL buffers taken) / hostheld (waiting, credits, a free controller buffer). Also credit returns.

Runs (7.5 ms, FSU off, peripheral-only resets after a discarded first connection), alternating:
  D  duplex: traced central + traced sink, n=4
  O  one-way control: traced central + traced sink with uplink off, n=2
  B  untraced reference (archived images), n=2 -> checks the tracer does not move the rates (within ~2 KB/s)

Pre-registered readings of the central's (downlink) distribution in D, each vs the one-way control O:
  H-credit  : cr0 is a large share in D (>= 25% of samples) and small in O -> the downlink waits for the sink's
              credit returns; then the sink's return rate / timing explains the cap.
  H-host    : hostheld is a large share in D (>= 25%) -> the open host does not hand data to a controller that
              has room (host TX scheduling, e.g. round-robin with the credit/signaling traffic).
  H-ctrl    : ctrlfull dominates in D (as in O) -> the controller holds downlink data yet sends only empty packets in
              ~1/3 of events: the cap is inside the open controller's TX scheduling.
  H-feed    : nofeed dominates -> the app doesn't keep the channel fed (unlikely: pool 16->64 had no effect).
More than one may contribute; the shares are reported as measured.

## Interim observation (written after reps 1-5 of the trace run, before the causal test below)
Duplex: central cr0 ~99%, its controller empty of downlink data ~35% of samples; sink never credit-starved (mean ~32),
its 64 controller ACL buffers full 95-100%. One-way control: central also cr0 ~98% but its controller is never empty
(credits return promptly, ~16 returns/s vs ~6/s in duplex). Tracer does not move the rates (57/117 = untraced).
Mechanism suspected: the sink's credit-return PDUs queue behind its ~64 buffered uplink packets in the open
controller (~130 ms at ~480 PDU/s), so the central drains and idles ~1/3 of the time. SDC's controller holds 20
packets (SDC_TX_PACKET_COUNT=20), which would make its return delay ~3x shorter (consistent with SDC's 1:1, untested).

## Causal test, pre-registered (2026-10-06, before running)
Variable: the sink's controller TX queue depth, CONFIG_BT_BUF_ACL_TX_COUNT 64 (as run) -> 20 (SDC's depth) -> 8
(ATT_TX_COUNT pinned at 64 so only this symbol differs; matched-pair verified). Traced central + traced sink; 7.5 ms,
FSU off; order 64, 20, 8, 8, 20, 64, 64, 20, 8, 8, 20, 64 (n=4 each).
Prediction (credit-return queueing): the downlink share rises monotonically as the queue shrinks (64: ~33%;
20: clearly higher, >= 40%; 8: higher still, toward 50%), with the central's controller-empty share falling and credit
returns per second rising. Falsified if the share stays ~33% at 8 and 20.
