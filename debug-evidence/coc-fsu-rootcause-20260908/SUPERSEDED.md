# ⛔ SUPERSEDED — conclusion overturned

FINDINGS.md here concluded "August +14.4% was a below-ceiling RF/session artifact; CoC FSU ~0% at
ceiling." **Wrong.** The "non-reproduction" was the auto-update-revert artifact: the replay SINKS had
`BT_GAP_AUTO_UPDATE_CONN_PARAMS=y`, so FSU reverted to 150 µs mid-run and the replays measured
post-revert throughput. With auto-update-OFF, held-verified sinks, CoC one-way FSU reproduces at
**≈+20%** (see `debug-evidence/coc-fsu-corrected-20260908/`). The hypothesis-elimination *method* here
was sound; the missing hypothesis was "the replay instrument doesn't hold FSU either." Retained as a
case study; do not cite its conclusions. See `docs/LESSONS.md` #1.
