## Latency sweep (RTT vs interval, both controllers)
Reliable range 7.5-37.5ms: open ~= SDC PARITY (12.3/27.3/47.5/73.4 vs 12.6/27.2/48.1/73.8),
RTT ~ 1.6-1.95x interval. >=50ms is RIG-LIMITED (z54-lat ping-timeout truncates long RTTs -> values
collapse, not real). The throughput-optimal operating points (15-25ms) fall in the reliable range
(RTT ~27-48ms). Latency is interval-bound + controller-independent (as established); the exact
long-interval RTT would need a longer ping-timeout in the rig.

## CoC-duplex (bespoke bidirectional-CoC build) + buffer re-test
**⚠️ SUPERSEDED 2026-08-14 — see `../coc-duplex-artifact-20260814/`.** The "~168 ≈ one-way" figure
below is an ARTIFACT: later DLE-instrumented debugging proved the bidirectional-CoC central received
ZERO uplink bytes (`CENRX cum_total=0` throughout), so ~168 was DOWNLINK-ONLY at the one-way rate,
not duplex. True bidirectional flow is ~90–100 KB/s agg, and it is capped by a peripheral L2CAP-TX
stall that triggers when the peripheral's DLE is extended to 251 (host never hands a segment down).
CoC-duplex is therefore under-replicated (one true point, 15ms n=2) AND open-stack-bug-limited — do
NOT treat the values below as a measured duplex result. (Original, now-known-wrong note follows.)

CoC-duplex ~= CoC one-way in aggregate (~168 peak @15ms open+FSU; SDC flat ~143) — the open-vs-SDC
crossover reappears. (Retracted an earlier "CoC-duplex ~103 inefficient" claim — that was an
unsettled smoke-test read.)
BUFFER RE-TEST (deep 96 TX buf / 192 credits, open CoC one-way): 50ms 132.5->142.8 (RECOVERS to
SDC's 143 ceiling); 100ms 107.9->114.1; 75ms 114->100 (n=1 noise). VERDICT: the open long-interval
CoC drop is SUBSTANTIALLY under-buffering (my coc app's 64 buffers), NOT a fundamental controller
deficit — deeper buffers recover 50ms fully and help 100ms. Residual 75-100ms gap possible but n=1
too noisy to attribute. => the sweep's long-interval open-CoC points are partly buffer-limited
artifacts; a clean long-interval comparison needs the deep-buffer build + replication.

## QUEUED: CoC-duplex credit-batching hypothesis (test after fair-tail)
CoC-duplex showed NO aggregate bump over one-way (~168 vs ~170), unlike GATT-duplex (~185 vs ~175,
+8%). Mechanism: GATT reverse = unacknowledged NOTIFY (credit-free) fills the peripheral's empty-ACK
slots for free; CoC reverse = credit-flow-controlled, and returning 1 credit/segment puts a credit
PDU on the air per data PDU (both directions) -> eats the reclaimed airtime. HYPOTHESIS: batching
credit returns (give N=16 at once instead of 1/segment) slashes credit-PDU count and recovers much
of the +8% bump. TEST: edit chan_seg_recv/seg_recv to accumulate + give_credits(N); re-measure CoC
duplex vs one-way; expect aggregate to rise toward GATT's +8%. n>=2. (Current CoC-duplex is n=1.)

## FAIR-TAIL (open-deep vs SDC, matched buffers, n=2, 50/75/100ms)
50ms: open-deep 146.0 vs SDC 142.2 (open RECOVERS/matches; was 132.5 under-buffered).
75ms: open-deep 123.9 vs SDC 140.8 (trails ~12%). 100ms: 120.3 vs 144.1 (trails ~16%).
VERDICT (nuanced, supersedes both earlier leans): the open CoC long-tail drop is PARTLY under-
buffering (fully recovered at 50ms) AND PARTLY a real residual open-controller event-fill limit at
75-100ms (~15% gap even deep-buffered; SDC's explicit MAX_CONN_EVENT_LEN extends huge events better
than open's fill-to-anchor). But 75-100ms is a falling-throughput regime, not an operating point.
Chart CoC one-way tail updated to the fair deep values (146/124/120). CoC-duplex tail still under-buffered.
