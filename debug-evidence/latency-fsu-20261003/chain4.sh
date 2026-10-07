#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q4
while ! grep -q '### CHAIN3 DONE' $S/chain3.log 2>/dev/null; do sleep 30; done
echo "### 8 LATENCY FSU (10-buffer queue)"; python3 tools/latency-fsu.py --out $S/latency --builds $S/b-lat2
echo "### CHAIN4 DONE"
