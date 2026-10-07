#!/bin/bash
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
echo "### PREREG2 START $(date)"
python3 tools/gatt-duplex-fsu.py --stack open --diag --intervals 9,11,13,14,15 --out <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q5/run --builds <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q5/b
echo "### PREREG2 DONE $(date)"
