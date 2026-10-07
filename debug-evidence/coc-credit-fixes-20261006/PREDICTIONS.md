# Pre-registered (2026-10-06, before the run): CoC duplex with the credit-counter fix
Bug fixed: coc-duplex-central's cen_avail (and the sinks' counters) were never reset on reconnect, so every
peripheral-only reset granted the sink extra credits. In coc-duplex-credit-trace-20261006 the sink's balance grew
32 -> 735, so the q20 result (1:1) ran with a sink credit surplus the recipe never intended.
Concern: with the fix the central's credit returns travel through the CENTRAL's own controller TX queue (64 deep);
once the downlink speeds up that queue fills, so the uplink could now be held back the same way.

Step A (this file): 7.5 ms, FSU off, fixed apps, CTRACE on both sides, alternating n=4 each:
  R  central queue 64 / sink 64 (recipe)
  S  central 64 / sink 20 (the configuration that gave 1:1 with the bug)
  B  central 20 / sink 20
Predictions:
  R: ~1:2 as before (the sink was never credit-limited, so the fix shouldn't change it): downlink share ~33%.
  S: if the credit-queueing mechanism is symmetric, the sink now waits on credits (sink cr0 high, its controller
     empty part of the time) and the split moves away from 1:1 (downlink share > 50%, possibly ~2:1).
     If S stays ~1:1 with the sink not credit-limited, the concern is unfounded.
  B: ~1:1, neither side's controller idles, aggregate ~184 KB/s.
Step B uses whichever of S/B is balanced (B if both) for the FSU re-measure at 7.5/15/25 ms, plus the recipe (R) and
SDC re-measured with the fixed apps.

## Step A result (written before the steps below)
Fixed cen_avail only: R 57/117 (32.9%), S 92/92 (49.8%), B 92/92 (49.8%), n=4 each, but the sink's credit balance still
grew ~60 per reconnect. Second leak found: with seg_recv the host never resets rx.credits, so the reused channel sent
the previous connection's leftovers as initial credits. Fixed (rx.credits zeroed before connect/accept). Check v2,
n=4 peripheral-only: balance flat at 64, but reps 1-2 had a total uplink stall (sink: data queued, 64 credits,
controller empty, nothing sent) and reps 3-4 ran 92/92 (1:1) with the recipe queues.
Code reading (subsys/bluetooth/host/l2cap.c, v4.4.2-16): with 0 initial credits the acceptor sets STATUS_OUT on
accept (tx_give_credits(0)); its first send lowers the channel from the ready list without clearing STATUS_OUT; later
credits only re-raise the channel if STATUS_OUT was clear -> permanent TX stall, racy (depends on whether the sink
queues data before the credits arrive). Our central granted its window only after connect (0 initial credits); the
leaked leftovers had hidden this on every connection except the first after a central boot.
Fix v3: central sends its 64-credit window in the connection request.

## Step W, pre-registered (before running): the reconnect wedge with v3
Recipe queues (64/64), CTRACE on, FSU off. (a) 7.5 ms cold (both boards reset each rep; the measured connection
is the first after central boot), n=6; (b) 7.5 ms peripheral-only, n=4; (c) 25 ms cold, n=4.
Prediction: 0 uplink stalls in all 14 reps (historically: --cold stalled most reps; 09-06 25 ms cold 10/12 stalled).
Split: ~1:1 (~92/92 at 7.5 ms) in all, since the 1:2 needed the leaked credit surplus.
Falsified if any rep stalls (then the wedge has another cause too) or if 1:2 returns.
