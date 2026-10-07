# Pre-registered (2026-10-06, before the run): CoC duplex FSU with all credit fixes
Fixes in the source (vs the images behind coc-duplex-fsu-matched-20261003 / duplex-fsu-followups / replication):
(1) central cen_avail reset per connection; (2) rx.credits zeroed before connect/accept (no stale leftover credits as
initial credits); (3) central sends its 64-credit window in the connection request (avoids the Zephyr host 0-initial-
credit stall); downlink rate from the cumulative byte counter (not the ~1% high per-second line median).
Run: tools/coc-duplex-fsu.py, open then SDC, 7.5/15/25 ms, ABBA x2 (n=4/arm), recipe queues, peripheral-only resets.
Predictions (from the whole-exchange model + today's fixed-app runs, which showed 1:1 at 92/92 (7.5 ms) and 78/78 (25 ms)):
- open: split ~1:1 at every interval (no one-way events) -> FSU follows the GATT duplex pattern: ~0% at 7.5 and 15 ms,
  ~+10% at 25 ms. (Published, bug-affected: +7.7 / +7.5 / +12.0% with a ~1:2 split.)
- SDC: ~1:1 as before; gains similar to published (-0.2 / +3.9 / +10-16%), since SDC's split didn't depend on the leak
  (its uplink/downlink were already balanced) - but the leak/initial-credit fixes may still shift it.
- No wedge stalls (0 rejects for stalls).
