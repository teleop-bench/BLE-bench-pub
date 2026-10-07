#!/bin/bash
# Stage 2b: intermittency probe for the peripheral DLE=251 stall. Firmware already flashed
# (s2-dxc/s2-dxs). Reset-cycle 3x (each reset re-runs channel-open = documented trigger).
# Stall signature: UP sent freezes, txcred frozen ~64, host_pulls flat. Healthy: all climb.
set -u
source $HOME/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
cd $HOME/Desktop/develop/zenoh-pico-ble-test
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
CEN=1057794857; PER=1057719509
CENP=/dev/cu.usbmodem0010577948573; PERP=/dev/cu.usbmodem0010577195093
OUT=/tmp/scratch
LOG=$OUT/stage2b-driver.log; : > $LOG
for i in 1 2 3; do
  echo "cycle $i" | tee -a $LOG
  python3 $CAP 22 $CENP:s2b${i}_cen $PERP:s2b${i}_per >/dev/null 2>&1 & C=$!
  sleep 5; nrfutil device reset --serial-number $PER >/dev/null 2>&1
  sleep 2; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
  wait $C
  # verdict: last UP sent value + whether it grew
  tail -2 $OUT/s2b${i}_per.log | grep -oE "UP sent=[0-9]+ eagain=[0-9]+ lasterr=[-0-9]+ txcred=[-0-9]+ host_pulls=[0-9]+" | tee -a $LOG
done
echo "=== STAGE2B DONE ===" | tee -a $LOG
