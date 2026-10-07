# Pre-registered (2026-10-06, before the run): one-way CoC FSU at 7.5 ms vs the sink's credit-return policy
Published (oneway-crossstack-20261005 and earlier): CoC FSU +0.0% at 7.5 ms on both stacks (open 157.0 -> 157.0,
SDC 125.5 -> 125.5), vs GATT +20-24%. Audit: the recipe sink (apps/coc/coc-sink) returns one credit per segment
(one 12-byte credit PDU per reply), so a 6th exchange no longer fits with 52 us gaps. Pilot (coc-duplex pair with
uplink off, batched credits): open 155.9 -> 184.3 KB/s (+18.2%, n=4).
Causal test: the recipe one-way CoC images exactly as oneway-fsu.py builds them ("max" profile, 7.5 ms), and the recipe
sink with only CONFIG_APP_CREDIT_BATCH=y added (matched pair). Both stacks, FSU off/on, n=4 per cell, interleaved.
Predictions:
  per-segment sink (recipe): reproduces ~0% gain on both stacks (open ~157 -> ~157, SDC ~125 -> ~125).
  batched sink: open gains ~+18-20% (~157 -> ~185); SDC gains clearly (> +10%) — SDC magnitude not predicted.
  FSU off with the batched sink: similar to per-segment or slightly higher (empty replies are shorter at 150 us too).
Falsified if the batched sink also shows ~0% on the open stack.
