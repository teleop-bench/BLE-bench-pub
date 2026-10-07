# Pre-registered (2026-10-06, before the runs): why is Zephyr CoC duplex at 25 ms FSU-off only ~155 KB/s?
Known: fixed apps, 25 ms: Zephyr off 154.5 / on 191.2; SDC off 186.3 / on 205.4; Zephyr 7.5/15 ms ~183 both arms;
Zephyr GATT duplex 25 ms off 184. FSU-on 191 ~ 10 exchanges/event; off 154.5 ~ 8.
A. Credit trace, Zephyr 25 ms, FSU off vs on (n=2 each, ABBA), CTRACE on both sides, fixed apps.
B. On-air (nRF52 observer, tools/onair-duplex.py --mode coc), Zephyr 15 ms (control) and 25 ms, off vs on, n=2 each.
Readings decided in advance:
 - CONTROLLER (events end early at 150 us gaps): on air FSU-off 25 ms events carry ~8 full exchanges vs ~10 with FSU
   (15 ms: 6 / 6), and CTRACE shows both sides with data queued in the controller (ctrlfull/cr0 with controller NOT
   empty; no idle-with-data) -> an open-controller scheduling effect at 25 ms / 150 us.
 - CREDITS (flow control): CTRACE shows a side at 0 credits with its controller EMPTY for a substantial share
   (>= ~15%) in FSU-off only, and on air FSU-off events are short / fewer, or many events one-way -> credit-return
   timing at 25 ms.
 - HOST (hostheld): data + credits + free controller buffers but not handed down -> host TX scheduling.
 - If on air shows ~10 exchanges per event in FSU off, the throughput loss is not per-event packing but missing events
   or retransmissions; then look at CRC-bad / event counts.

## Results so far + next check (written before the event-counter run)
A (credit trace, n=2/arm): both arms identical: both sides ~100% at 0 credits but neither controller ever empty, no
host-held data -> credits/host ruled out. B (on air): 25 ms FSU off = 10 full exchanges/event (on: 11; 15 ms: 6/6),
0 retransmissions, no empty data packets -> per-event packing is NOT the loss. Rates deliver only 82% of 10 exchanges
per event at 25 ms FSU off (FSU on 92%, 7.5/15 ms 97%) -> events must be missing.
C (pre-registered): open controller's own counter (CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y on the central; counts closed
events), 25 ms, n=2/arm. Prediction: FSU off closes ~82% of the 400 events per 10 s (~330), FSU on ~92% (~370).
If both are ~400, the loss is elsewhere (e.g. data-less events or wasted exchanges the observer can't see).

## C result + causal test D (written before D runs)
C: events closed per 10 s ~400 in both arms (no missing events -> C's prediction failed), but the controller's mean
exchanges/event is 8.2-8.3 (off) vs 10.2-10.4 (on), ratio 0.80 = the throughput ratio. On air the FSU-off events are
bimodal: ~40 full (10 exchanges) + ~13 that end after 2-4 exchanges with MD=0 on both sides (both radio queues dry);
FSU on: 9-11 throughout. Interpretation (hypothesis): credit-loop starvation - each side's credit returns wait behind
its own data in the 64-deep controller FIFO, so both sides periodically run out of credited data together. (CTRACE's
"controller not empty" counts buffers not yet completed, which includes sent-but-unacknowledged ones, so it could not
see the radio queue going dry.)
D (pre-registered): 25 ms, controller TX queue 20 on BOTH sides (BT_BUF_ACL_TX_COUNT=20, ATT_TX_COUNT=64), FSU off vs on,
n=2/arm. Prediction if credit-loop starvation: FSU off rises from ~155 toward the 10-exchange ceiling (~180-189),
FSU on rises toward ~200-208, and the FSU gain falls toward the model's ~+10%. Falsified if FSU off stays ~155.

## D result + E (written before E runs)
D: 20-deep queues both sides, 25 ms: FSU off 184.5/184.8, on 204.0/205.1 -> +10.8%, = SDC (186.3/205.4, +10.2%) and the
model (~+10%). Prediction held: the 25 ms anomaly was credit-loop starvation from the 64-deep controller FIFO.
E (pre-registered): tools/coc-duplex-fsu.py --stack open, 7.5/15/25 ms, n=4/arm, 20-deep queues on both sides.
Prediction: 7.5 and 15 ms unchanged from the 64-deep run (~183, ~0% gain); 25 ms ~185 -> ~205 (~+10%).
