#!/bin/sh
# Second-day replication, started unattended at 06:45 (2026-10-07). Run under caffeinate.
cd <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/tree
export PATH=<ZEPHYR_WS>/.venv/bin:$PATH ZEPHYR_BASE=<ZEPHYR_WS>/zephyr
python3 -c "import datetime,time; t=datetime.datetime(2026,10,7,6,45); d=(t-datetime.datetime.now()).total_seconds(); print('sleeping',int(d),'s until',t,flush=True); time.sleep(max(0,d))"
echo "START $(date)"; git log --oneline -1
python3 tools/coc-duplex-fsu.py --stack open --intervals 6,12,20 --builds <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/b-open --out <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/coc-open > <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/coc-open.log 2>&1; echo "coc-open rc=$? $(date)"
python3 tools/coc-duplex-fsu.py --stack sdc --ncs-root <NCS_WS> --intervals 6,12,20 --builds <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/b-sdc --out <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/coc-sdc > <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/coc-sdc.log 2>&1; echo "coc-sdc rc=$? $(date)"
python3 tools/oneway-fsu.py --transports coc --coc-credit-batch --phase sweep --ncs-root <NCS_WS> --builds <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/b-ow --out <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/oneway-coc > <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/oneway-coc.log 2>&1; echo "oneway-coc rc=$? $(date)"
echo "DONE $(date)"; touch <SCRATCH>/b2777530-2abd-4537-b0c5-d116f2eaefd4/scratchpad/rep1007/DONE
