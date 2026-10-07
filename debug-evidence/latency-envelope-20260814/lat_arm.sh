#!/bin/zsh
# one RTT arm: flash periph fresh (echo) + central hex (RTT driver), capture central console.
# $1=label $2=central-hex
set -e
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null || true
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
CEN=/dev/cu.usbmodem0010577948573; PER=/dev/cu.usbmodem0010577195093
LABEL=$1; HEX=$2
west flash -d /tmp/lat-per-build --no-rebuild --hex-file /tmp/lat-per.hex --dev-id 1057719509 >/tmp/f-$LABEL-per.log 2>&1
west flash -d /tmp/lat-cen-build --no-rebuild --hex-file $HEX --dev-id 1057794857 >/tmp/f-$LABEL-cen.log 2>&1
python3 $CAP 45 ${CEN}:${LABEL}-cen ${PER}:${LABEL}-per
echo "[lat $LABEL] done"
