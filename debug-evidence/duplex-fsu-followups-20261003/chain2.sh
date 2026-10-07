#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q4
while ! grep -q '### CHAIN DONE' $S/chain.log; do sleep 30; done
echo "### 5 N=8 SDC CoC 15 ms"; python3 tools/coc-duplex-fsu.py --stack sdc --intervals 12 --abba 4 --out $S/coc-sdc15-n8 --builds $S/../coc/b-sdc --skip-build
echo "### 6 N=8 open CoC 25 ms"; python3 tools/coc-duplex-fsu.py --stack open --intervals 20 --abba 4 --out $S/coc-open25-n8 --builds $S/../coc/b-open --skip-build
echo "### CHAIN2 DONE"
