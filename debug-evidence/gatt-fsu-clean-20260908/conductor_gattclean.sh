#!/bin/bash
# Hands-off clean-GATT run: 1-rep SMOKE across all 20 cells -> if enough accept, full 8-rep run.
# Boards are free (build done). Keeps the mac awake.
export PATH=/usr/local/bin:$PATH
R=<REPO>
D=$R/debug-evidence/gatt-fsu-clean-20260908
say(){ echo "[$(date +%H:%M)] $*" | tee -a "$D/conductor.log"; }
pkill -f "caffeinate -dimsu" 2>/dev/null; nohup caffeinate -dimsu >/dev/null 2>&1 &

say "SMOKE: 1 rep x 20 cells"
python3 "$D/harness.py" 1 >> "$D/conductor.log" 2>&1
acc=$(python3 -c "import json;print(sum(1 for l in open('$D/results.jsonl') if json.loads(l)['pass']))" 2>/dev/null || echo 0)
say "smoke accepted $acc/20"
if [ "${acc:-0}" -lt 14 ]; then say "ABORT: smoke too weak ($acc/20) — not running full"; exit 1; fi
say "FULL: 8 reps counterbalanced"
python3 "$D/harness.py" 8 >> "$D/conductor.log" 2>&1
say "FULL done"
echo "===== SUMMARY ====="; cat "$D/summary.txt"
say "CLEAN-GATT COMPLETE"
