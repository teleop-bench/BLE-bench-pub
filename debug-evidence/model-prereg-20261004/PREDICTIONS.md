# Pre-registered predictions (written 2026-10-04 ~15:35, before any run at these intervals)
Model (unchanged since 2026-10-03): full exchanges per event = floor((T - M) / t_exch),
t_exch = 2396 us (150 us gap) / 2200 us (52 us gap), end-of-event reserve M = 250 us (bounded 200 < M < 312 by
the 10-03 counts). GATT echo duplex, open Zephyr, on-chip exchange counter on (--diag). Boards at the
2026-10-04 placement (further apart than 10-03).

| interval | units | predicted exchanges off -> on | predicted gain | robust to M in 200..312? |
|---|---|---|---|---|
| 11.25 ms | 9  | 4 -> 5 | +25%   | NO: knife-edge (5th exchange needs M <= 250); M=312 gives 4 -> 4, 0% |
| 13.75 ms | 11 | 5 -> 6 | +20%   | yes |
| 16.25 ms | 13 | 6 -> 7 | +16.7% | yes |
| 17.5 ms  | 14 | 7 -> 7 | 0%     | yes (control: predicted no gain) |
| 18.75 ms | 15 | 7 -> 8 | +14.3% | yes |

Falsified if the on-chip mode exchange counts differ from these at 13.75 / 16.25 / 17.5 / 18.75 ms, or the
throughput gain is clearly inconsistent in sign or size. 11.25 ms is a measurement of M rather than a pass/fail test.
