# Pre-registered (2026-10-06 evening, before the run): second-day replication of the corrected CoC results
Run starts 2026-10-07 06:45, unattended, boards NOT moved (same placement as 2026-10-06: close range, ~3-4 in apart).
Same firmware recipes, rebuilt from a pinned git worktree of the merged commit (rep1007/tree) (open CoC duplex apps now carry 20-deep queues).
1. tools/coc-duplex-fsu.py --stack open, 7.5/15/25 ms, n=4/arm
2. tools/coc-duplex-fsu.py --stack sdc,  7.5/15/25 ms, n=4/arm
3. tools/oneway-fsu.py --transports coc --coc-credit-batch --phase sweep, both stacks, 7.5-50 ms, n=4/cell
Predictions (verdict-level, as for replication-20261004): every cell keeps its verdict -
 duplex: ~0% at 7.5 ms on both stacks, small (+2-3%) at 15 ms, ~+10% at 25 ms; split ~1:1; Zephyr within ~2 KB/s of SDC.
 one-way CoC: FSU gain ~+20% (Zephyr) / ~+25% (SDC) at 7.5 ms; SDC 15 ms FSU-off ~156 (parity with Zephyr);
 gains within ~2 points of 2026-10-06 per cell. Absolute rates may move by a few KB/s with the RF day.
A cell "fails to replicate" if its gain moves by more than its 10-06 95% interval + 2 points, or a split leaves ~1:1.
