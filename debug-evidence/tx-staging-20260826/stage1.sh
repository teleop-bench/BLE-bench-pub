#!/bin/bash
# Stage 1: config-only refill-wall probe. Central downlink, SDU=480, diag ON.
# Core discriminator = INTERVAL sweep (count-cap vs airtime). + shallow-buffer control.
# Metric: mean_pkts/ev from central "RPT occ" lines. Sink built auto-update=n so interval holds.
set -u
source $HOME/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
cd $HOME/Desktop/develop/zenoh-pico-ble-test
B=nrf54l15dk/nrf54l15/cpuapp
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
CEN=1057794857; PER=1057719509
CENP=/dev/cu.usbmodem0010577948573; PERP=/dev/cu.usbmodem0010577195093
OUT=/tmp/scratch
LOG=$OUT/stage1-driver.log
: > $LOG
say(){ echo "[$(printf '%(%H:%M:%S)T')] $*" | tee -a $LOG; }

bld(){ # name extra-defines...
  local name=$1; shift
  west build -p always -b $B -d /private/tmp/$name coc-central -- \
    -DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y -DCONFIG_APP_SDU_SIZE=480 "$@" \
    >$OUT/build-$name.log 2>&1 && say "BUILT $name" || { say "BUILD FAIL $name"; grep -E "error:|Error" $OUT/build-$name.log | head -3 | tee -a $LOG; }
}

say "=== building sink (auto-update=n so central interval pins) ==="
west build -p always -b $B -d /private/tmp/s1-snk coc-sink -- \
  -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n >$OUT/build-s1-snk.log 2>&1 \
  && say "BUILT s1-snk" || { say "SINK BUILD FAIL"; grep -E "error:" $OUT/build-s1-snk.log|head; exit 1; }

say "=== building central arms ==="
bld s1-A1 -DCONFIG_APP_CONN_INT_UNITS=6  -DCONFIG_BT_BUF_ACL_TX_COUNT=64 -DCONFIG_BT_CONN_TX_MAX=64
bld s1-A2 -DCONFIG_APP_CONN_INT_UNITS=12 -DCONFIG_BT_BUF_ACL_TX_COUNT=64 -DCONFIG_BT_CONN_TX_MAX=64
bld s1-A3 -DCONFIG_APP_CONN_INT_UNITS=20 -DCONFIG_BT_BUF_ACL_TX_COUNT=64 -DCONFIG_BT_CONN_TX_MAX=64
bld s1-B1 -DCONFIG_APP_CONN_INT_UNITS=12 -DCONFIG_BT_BUF_ACL_TX_COUNT=8  -DCONFIG_BT_CONN_TX_MAX=8

# flash sink once (constant across arms)
say "=== flash sink ==="
west flash -d /private/tmp/s1-snk --dev-id $PER --no-rebuild 2>&1 | tail -1 | tee -a $LOG

run(){ # name label
  local name=$1; local lbl=$2
  [ -f /private/tmp/$name/zephyr/zephyr.hex ] || { say "SKIP $name (no hex)"; return; }
  say "--- arm $name -> flash central + reset + capture 42s ---"
  west flash -d /private/tmp/$name --dev-id $CEN --no-rebuild 2>&1 | tail -1 | tee -a $LOG
  python3 $CAP 42 $CENP:${lbl}_cen $PERP:${lbl}_per >/dev/null 2>&1 &
  local C=$!
  sleep 6;  nrfutil device reset --serial-number $PER >/dev/null 2>&1
  sleep 3;  nrfutil device reset --serial-number $CEN >/dev/null 2>&1
  wait $C
  say "captured $lbl"
}

run s1-A1 s1a1
run s1-A2 s1a2
run s1-A3 s1a3
run s1-B1 s1b1
say "=== STAGE1 CAPTURES DONE ==="
