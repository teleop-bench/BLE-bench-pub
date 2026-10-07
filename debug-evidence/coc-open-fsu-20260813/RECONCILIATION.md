# Reconciliation: the 131-vs-157 baseline gap, and when FSU actually helps

The first CoC±FSU run (50 ms/244 B) gave FSU-off ~131 KB/s — below the charted "open·CoC·157".
Reconciled by re-running at the ORIGINAL 157 regime (**7.5 ms interval / 480 B SDU**, per
[[nrf54l15-ble6-benchmark]] "2M/DLE-251/7.5ms, CoC downlink 480B ~158"). Same 2 boards, same
firmware, only interval+SDU changed.

## 1. The baseline gap = connection interval
- 50 ms/244 B: FSU-off **131** KB/s (events run long, mean 28 / max 34 packets/event).
- 7.5 ms/480 B: FSU-off **~143** KB/s (ABBA mean of 145.9, 139.2; events short, **max 5 packets**).
- 7.5 ms raised the baseline toward 157. Residual ~143-vs-157 (~9 %) = ambient 2.4 GHz on the day
  (goodput is noisy, 139–146) + segmentation; not a contradiction. **Interval is the driver.**

## 2. When FSU converts — regime-dependent, and NOT at the throughput peak
Drift-cancelled ABBA (f150,f52,f52,f150), receiver-delivered goodput:

| regime            | FSU-off | FSU-on | gain   | occupancy (off→on) | verdict |
|-------------------|---------|--------|--------|--------------------|---------|
| **7.5 ms / 480 B**| 142.6   | 145.3  | +1.9 % (arms OVERLAP) | max 5 → max 5 (unchanged) | **NO real gain** |
| **50 ms / 244 B** | 131.3   | 150.2  | +14.4 % (strict non-overlap) | max 34 → max 40 | real gain |

**Physical explanation (airtime arithmetic).** A 251 B PDU at 2M ≈ 1.0 ms on air; a transaction
(data + tIFS + ACK + tIFS) ≈ 1.34 ms. FSU cuts tIFS 150→52 µs, saving ~0.2 ms/transaction.
- 7.5 ms event fits ~5 transactions → total saving ~1 ms, not reliably enough for a 6th packet,
  and the controller did NOT add one (max stayed 5). ⇒ ~0 % gain.
- 50 ms event fits ~34 transactions → saving ~6.7 ms → ~6 more packets (34→40). ⇒ +14 %.
So **FSU's benefit scales with packets-per-event**: negligible at the short-interval throughput
peak, real at long-interval airtime-bound events.

## 3. Corrected headline conclusion (supersedes the earlier "+20 % beats open")
- **FSU does NOT raise the throughput CEILING on this open controller.** The max throughput is at
  7.5 ms (~146–157 KB/s), where FSU gives ~0 %.
- The earlier +14.4 % (CoC) / +20 % (GATT) are REAL but occur only at 50 ms, whose absolute
  throughput (150) is still BELOW the 7.5 ms ceiling (157). FSU recovers throughput within a
  lower-throughput regime; it does not beat the plain-Zephyr max.
- ⇒ **The original chart's "open + FSU · CoC · 157 · no 2M gain" is CORRECT for the 7.5 ms
  throughput regime.** My mid-session "+20 % beats open" was regime-blind and is retracted.

## 4. Open sub-question (not chased)
Why the 7.5 ms event doesn't add a 6th packet when FSU frees ~1 ms (~enough for one): likely a
controller event-length / MD-chaining schedule computed on the 150 µs assumption. If tunable, it
might unlock some FSU gain at 7.5 ms — unverified, a future lever.

Evidence: `reconcile-7p5ms/` (4 arms). Same rig/firmware as the parent dir; interval 6 (7.5 ms),
SDU 480 B, sink PREF 6/6.
