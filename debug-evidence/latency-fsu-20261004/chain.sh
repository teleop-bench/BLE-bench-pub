#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q6
echo "### L1 SDC latency 7.5 naive/paced + 25 naive ($(date))"; python3 tools/latency-fsu.py --stack sdc --ncs-root <NCS_WS> --out $S/sdc --builds $S/b-sdc
echo "### L2 open latency + counter, standard ramp 7.5 naive/paced ($(date))"; python3 tools/latency-fsu.py --diag --configs 6:naive,6:polite --out $S/diag --builds $S/b-diag
echo "### L3 open latency + counter, high ramp 7.5 naive ($(date))"; python3 tools/latency-fsu.py --diag --high --configs 6:naive --out $S/diag-high --builds $S/b-diag-high
echo "### CHAIN DONE $(date)"
