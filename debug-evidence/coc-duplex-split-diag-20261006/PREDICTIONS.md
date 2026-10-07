# Pre-registered (2026-10-06, before the run)
Question: why does open-stack CoC duplex split ~1:2 (central->sink downlink ~55 KB/s vs sink->central uplink ~114)?
Known before the run: no credit waits on either side (central eagain=0; sink txcred 29-63); apps asymmetric
(central app pool 16 vs sink 64; central controller RX buffers 1 vs sink 8; central extra host ACL RX 1 vs sink 8);
SDC splits 1:1 with the same apps.
Test: open CoC duplex, 7.5 ms, FSU off; central "baseline" (as archived) vs "rxbuf" (BT_CTLR_RX_BUFFERS=8,
BT_BUF_ACL_RX_COUNT_EXTRA=8, matching the sink); alternating, n=4 each; peripheral-only resets after a discarded
first connection.
Hypothesis H1 (central receive starvation): rxbuf moves the split from ~1:2 toward ~1:1 (downlink share rises
from ~33% toward ~50%). Falsified if the downlink share stays ~33% (then test H2: central app pool 16 -> 64).

## Result of test 1 (H1): FALSIFIED. Downlink share 32.8% with both baseline and rxbuf (n=4 each).
## Test 2, pre-registered before running (2026-10-06): H2, the central's 16-SDU app pool
The pool depth is a documented "duplex fix" in coc-duplex-central (16 chosen because 64 starved the peer's uplink).
Variants: baseline (pool 16, byte-identical to the archived image) vs pool64 (CONFIG_APP_TX_POOL_COUNT=64, the
sink's depth); otherwise identical (matched-pair verified); same procedure, ABBA x2.
Prediction if H2: pool64 raises the downlink share well above 33% (toward 50%), possibly at the cost of uplink
(as the code comment warns: uplink may starve or stall). Falsified if the downlink share stays ~33%.

## Result of test 2 (H2): FALSIFIED. Downlink share 32.7% with pool 16 and pool 64 (n=4 each).
Observation: the downlink is a hard cap of ~57 KB/s in all 16 reps (~0.9 x 480-byte SDU per 7.5 ms event); the
sink's uplink runs ~486 SDUs/s of 244 bytes (one LL packet each).
## Test 3, pre-registered before running (2026-10-06): H3, SDU size / segmentation
The central sends 480-byte SDUs (two LL packets, host-segmented); the sink sends 244-byte SDUs (one packet).
Variants: baseline (480, byte-identical) vs sdu244 (CONFIG_APP_DUPLEX_SDU_SIZE=244); matched-pair verified; same procedure.
Prediction if H3 (segmented SDUs are serialized ~1 per event on the open stack): with 244-byte SDUs the downlink
cap lifts and the split moves toward ~1:1 (downlink share well above 33%). Falsified if it stays ~33% / ~57 KB/s.

## Result of test 3 (H3): FALSIFIED. Downlink share 32.7% (480 B) vs 33.1% (244 B), n=4 each.
Downlink stays ~57-58 KB/s (~2 LL data packets per 7.5 ms event on average) regardless of receive buffers, app pool
depth and SDU size; alone (one-way) the same central sends ~150 KB/s.
## Test 4, pre-registered before running (2026-10-06): H4, CPU/thread scheduling (observation, not A/B)
Zephyr thread analyzer (CONFIG_THREAD_ANALYZER + runtime stats, auto every 5 s) on both boards, baseline traffic.
Prediction if H4: the central's CPU is near-saturated (idle thread ~0%) with Bluetooth RX threads taking most of it,
while the sink keeps idle headroom; the split stays ~1:2. Falsified if the central shows substantial idle time.

## Result of test 4 (H4): FALSIFIED. Thread analyzer: central idle 87%, sink idle 85% (ISR time may count as idle);
no CPU asymmetry. CORRECTION: credit starvation was NOT ruled out: Zephyr 4.4 bt_l2cap_chan_send queues SDUs when
credits run out instead of returning -EAGAIN, so the central's eagain=0 counter could never fire.
## Test 5, pre-registered before running (2026-10-06): H5, the sink's credit returns queue behind its deep uplink pool
The sink returns credits (batch, when < 24 of 64 remain) as an L2CAP PDU on its own TX path, behind a 64-SDU uplink
pool that its code comment says should be 16 ("DUPLEX FIX ... 16 << 64-credit window"; the code has 64).
Variants: sink pool 64 (default, byte-identical) vs pool16 (CONFIG_APP_TX_POOL_COUNT=16); central baseline; same procedure.
Prediction if H5: with pool16 the central gets credits promptly, the downlink cap lifts and the split moves toward
~1:1 (downlink share well above 33%). Falsified if it stays ~33% / ~57 KB/s.
