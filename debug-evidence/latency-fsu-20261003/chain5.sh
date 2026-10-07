#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q4
while ! grep -q '### CHAIN4 DONE' $S/chain4.log 2>/dev/null; do sleep 30; done
echo "### 9 LATENCY FSU second ABBA block (-> n=4 per arm)"; python3 tools/latency-fsu.py --out $S/latency-b --builds $S/b-lat2 --skip-build
echo "### 10 LATENCY FSU high load 0/140..190 KB/s"; python3 tools/latency-fsu.py --high --out $S/latency-high --builds $S/b-lathigh
echo "### CHAIN5 DONE"
