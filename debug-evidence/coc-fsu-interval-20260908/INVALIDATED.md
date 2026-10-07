# ⛔ INVALIDATED — do not use these numbers

This "CoC FSU flat at 15/25/37.5/50 ms" sweep is **invalid**. The sink had
`BT_GAP_AUTO_UPDATE_CONN_PARAMS=y`, so the ~5 s GAP param-update **silently reverted tIFS to 150 µs
mid-run** at every interval while the latched `fsu=52` token kept printing — so this measured
post-revert (150 µs) throughput and wrongly read "flat." The harness verified FSU by a boot-time token,
not that it HELD (the core mistake; see `docs/LESSONS.md` #1).

**Corrected, held-verified result:** CoC one-way FSU ≈ **+20%** — see
`debug-evidence/coc-fsu-corrected-20260908/` (auto-update-OFF sink, held end-to-end, `verify_run`-gated).
Kept only as the worked example behind the lesson + the `tools/verify_run.py` `revert_coc_on` regression fixture.
