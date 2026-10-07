#!/bin/zsh
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null || true
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
LABEL=$1; CHEX=$2; PHEX=$3
west flash -d /tmp/hh-open-per --no-rebuild --hex-file $PHEX --dev-id 1057719509 >/tmp/hf-$LABEL-p.log 2>&1
west flash -d /tmp/hh-open-cen --no-rebuild --hex-file $CHEX --dev-id 1057794857 >/tmp/hf-$LABEL-c.log 2>&1
python3 $CAP 55 /dev/cu.usbmodem0010577948573:${LABEL}-cen /dev/cu.usbmodem0010577195093:${LABEL}-per
echo "[$LABEL] done"
