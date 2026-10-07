#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q4
while ! grep -q '### CHAIN2 DONE' $S/chain2.log 2>/dev/null; do sleep 30; done
echo "### 7 PRE-REGISTERED MODEL TEST (12.5/22.5/35 ms)"; python3 tools/gatt-duplex-fsu.py --stack open --diag --intervals 10,18,28 --out $S/model-prereg --builds $S/b-prereg
echo "### CHAIN3 DONE"
