# Pre-registered predictions: why FSU lowers stop-signal latency (written 2026-10-04 ~16:30, before the run)
Hypothesis: under load the bulk stream fills connection events. FSU shortens each bulk exchange (a full 251-byte
packet + the peer's empty reply + 2 gaps: 1048 + 44 + 2g us = 1392 us at 150 us, 1196 us at 52 us), so more fit per
event; the bulk queue drains faster and the stop-signal ping waits less.
Same whole-exchange model as the duplex runs (M = 250-312 us):
| interval | full bulk events: transactions off -> on | implied ceiling ratio |
|---|---|---|
| 7.5 ms | 5 -> 6 | +20% (measured ceilings 10-03: ~150 -> ~180 KB/s) |
Expected in the open controller's counter (CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG): at loaded stages the largest common
event size ("full", >= 5% of events) is 5 with FSU off and 6 with FSU on at 7.5 ms, naive and paced, and the mean
transactions per event at a given load is the same in both arms below the ceiling (same bulk delivered), while the
FSU-on arm needs fewer full events per burst. Falsified if full-event sizes do not differ (5 vs 6) under load.
