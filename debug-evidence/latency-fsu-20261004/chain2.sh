#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q6
while ! grep -q '### CHAIN DONE' $S/chain.log; do sleep 30; done
echo "### L1b SDC latency 7.5 naive/paced + 25 naive ($(date))"; python3 tools/latency-fsu.py --stack sdc --ncs-root <NCS_WS> --out $S/sdc --builds $S/b-sdc
echo "### CHAIN2 DONE $(date)"
