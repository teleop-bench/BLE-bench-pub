# Pre-registered predictions (written 2026-10-03 before the run)
Model: full exchanges per event n = floor((T_interval - M) / t_exch), with
t_exch = 2x1048 us + 2 x gap (gap 150 -> 2396 us; 52 -> 2200 us) and an end-of-event reserve M ~ 250 us
(bounded 200 < M < 312 us by the 7.5/10/15/20/25/30/50 ms counts). Throughput gain ~ ratio of mean exchanges.
| interval | units | predicted exchanges off -> on | predicted gain |
|---|---|---|---|
| 12.5 ms | 10 | 5 -> 5 (12250/2396 = 5.11; 12250/2200 = 5.57) | ~0% |
| 22.5 ms | 18 | 9 -> 10 (22250/2396 = 9.29; 22250/2200 = 10.11) | ~+11% |
| 35 ms   | 28 | 14 -> 15 (34750/2396 = 14.50; 34750/2200 = 15.80) | ~+7% |
Falsified if the measured mode exchanges differ from these, or the gain sign/size is clearly off.
