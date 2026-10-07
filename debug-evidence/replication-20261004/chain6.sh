#!/bin/bash
# Replication session (2026-10-04): fresh builds, same recipes, a different day.
cd <REPO>
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
S=<SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/q4; R=$S/rep
# (time gate removed: boards re-spaced at 08:18, started early)
while ! grep -q '### CHAIN5 DONE' $S/chain5.log 2>/dev/null; do sleep 60; done
mkdir -p $R; echo "placement: nRF54 boards moved further apart by the user before this session (2026-10-04 ~08:15)"; echo "### REP START $(date)"; nrfutil device list | grep -E '^[0-9]{6,}|PCA'
echo "### R1 GATT duplex open 7.5/15/25"; python3 tools/gatt-duplex-fsu.py --stack open --intervals 6,12,20 --out $R/gatt-open --builds $R/b-gatt-open
echo "### R2 GATT duplex SDC 7.5/15/25"; python3 tools/gatt-duplex-fsu.py --stack sdc --intervals 6,12,20 --out $R/gatt-sdc --builds $R/b-gatt-sdc --ncs-root <NCS_WS>
echo "### R3 CoC duplex open 7.5/15/25"; python3 tools/coc-duplex-fsu.py --stack open --intervals 6,12,20 --out $R/coc-open --builds $R/b-coc-open
echo "### R4 CoC duplex SDC 7.5/15/25"; python3 tools/coc-duplex-fsu.py --stack sdc --intervals 6,12,20 --out $R/coc-sdc --builds $R/b-coc-sdc --ncs-root <NCS_WS>
echo "### R5 latency FSU 7.5 naive/paced, 25 naive"; python3 tools/latency-fsu.py --out $R/latency --builds $R/b-lat
echo "### CHAIN6 DONE $(date)"
