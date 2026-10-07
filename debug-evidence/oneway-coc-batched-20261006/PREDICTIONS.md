# Pre-registered (2026-10-06, before the run): one-way CoC FSU sweep with batched credit returns
Same as oneway-crossstack-20261005's CoC sweep (tools/oneway-fsu.py, "max" profile, both stacks, 7.5/15/25/37.5/50 ms,
n=4, interleaved) except the CoC sinks return credits in batches (--coc-credit-batch). Sweep phase only.
Predictions: 7.5 ms ~+20% (Zephyr) / ~+25% (SDC) as in coc-credit-policy-20261006; 15-50 ms gains equal to or larger
than the published per-segment values (Zephyr +13.6 to +20.1%, SDC +11.3 to +18.5%), approaching the GATT pattern;
FSU-off rates ~unchanged from published (within ~1-2 KB/s, now measured from the cumulative counter).
