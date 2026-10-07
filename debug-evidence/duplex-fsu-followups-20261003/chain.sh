#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q4
echo "### 1 MODEL TEST (GATT diag 10/20/30/50 ms)"; python3 tools/gatt-duplex-fsu.py --stack open --diag --intervals 8,16,24,40 --out $S/model-test --builds $S/b-model
echo "### 2 COC COLD (7.5/15 ms)"; python3 tools/coc-duplex-fsu.py --stack open --cold --intervals 6,12 --out $S/coc-cold --builds $S/../coc/b-open --skip-build
echo "### 3 ON-AIR COC (7.5/15/25 ms)"; python3 tools/onair-duplex.py --mode coc --builds $S/../coc/b-open --obs-build build/observer-smoke-2m/obs --out $S/onair-coc --intervals 6,12,20
echo "### 4 LATENCY FSU"; python3 tools/latency-fsu.py --out $S/latency --builds $S/b-lat
echo "### CHAIN DONE"
