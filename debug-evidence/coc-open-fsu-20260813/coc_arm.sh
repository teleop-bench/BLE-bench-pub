#!/bin/zsh
# one CoC ±FSU arm: flash central hex (port closed), then capture from boot.
# $1=label  $2=hex
set -e
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null || true
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
CEN=/dev/cu.usbmodem0010577948573
SINK=/dev/cu.usbmodem0010577195093
LABEL=$1; HEX=$2
echo "[arm $LABEL] flashing SINK fresh (clean advertise state)..."
west flash -d /tmp/coc-sink-app/build --no-rebuild --hex-file /tmp/coc-sink-app/build/zephyr/zephyr.hex --dev-id 1057719509 >/tmp/flash-$LABEL-sink.log 2>&1
echo "[arm $LABEL] flashing central..."
west flash -d /tmp/coc-cen-app/build --no-rebuild --hex-file $HEX --dev-id 1057794857 >/tmp/flash-$LABEL.log 2>&1
echo "[arm $LABEL] capturing 75s (central connects ~12s in)..."
python3 $CAP 75 ${CEN}:${LABEL}-cen ${SINK}:${LABEL}-sink
echo "[arm $LABEL] done"
