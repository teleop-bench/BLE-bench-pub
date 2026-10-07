#!/bin/bash
# Stage 1b: DIRECT mechanism test — FSU on vs off at matched 15ms/deep(64)/SDU480/10cm.
# airtime/tIFS-bound => FSU raises mean_pkts/ev ~+15% (tIFS 150->52us). staging-throttle => ~0%.
# ABBA: fon/foff/fon/foff. foff reuses /private/tmp/s1-A2. Sink rebuilt FSU-capable.
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
LOG=$OUT/stage1b-driver.log; : > $LOG
say(){ echo "$*" | tee -a $LOG; }

say "=== build FSU-capable sink (auto-update=n) ==="
west build -p always -b $B -d /private/tmp/s1-snkf coc-sink -- \
  -DCONFIG_BT_FRAME_SPACE_UPDATE=y -DCONFIG_BT_CTLR_FRAME_SPACE_UPDATE=y \
  -DCONFIG_BT_LE_EXTENDED_FEAT_SET=y -DCONFIG_BT_CTLR_EXTENDED_FEAT_SET=y \
  -DCONFIG_BT_CTLR_FSU_BENCH_FORCE_FEAT=y -DCONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US=52 \
  -DCONFIG_BT_CTLR_ADVANCED_FEATURES=y -DCONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y \
  -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n >$OUT/build-s1-snkf.log 2>&1 \
  && say "BUILT s1-snkf" || { say "SINKF FAIL"; grep -E "error:" $OUT/build-s1-snkf.log|head; exit 1; }

say "=== build FSU-on central (15ms deep diag) ==="
west build -p always -b $B -d /private/tmp/s1-fon coc-central -- \
  -DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y -DCONFIG_APP_SDU_SIZE=480 \
  -DCONFIG_APP_CONN_INT_UNITS=12 -DCONFIG_BT_BUF_ACL_TX_COUNT=64 -DCONFIG_BT_CONN_TX_MAX=64 \
  -DEXTRA_CONF_FILE=open-fsu.conf >$OUT/build-s1-fon.log 2>&1 \
  && say "BUILT s1-fon" || { say "FON FAIL"; grep -E "error:" $OUT/build-s1-fon.log|head; exit 1; }

say "=== flash FSU sink ==="
west flash -d /private/tmp/s1-snkf --dev-id $PER --no-rebuild 2>&1 | tail -1 | tee -a $LOG

run(){ local dir=$1 lbl=$2
  west flash -d /private/tmp/$dir --dev-id $CEN --no-rebuild 2>&1 | tail -1 | tee -a $LOG
  python3 $CAP 42 $CENP:${lbl}_cen $PERP:${lbl}_per >/dev/null 2>&1 & local C=$!
  sleep 6; nrfutil device reset --serial-number $PER >/dev/null 2>&1
  sleep 3; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
  wait $C; say "captured $lbl"
}
run s1-fon  s1_fon_A
run s1-A2   s1_foff_B
run s1-fon  s1_fon_C
run s1-A2   s1_foff_D
say "=== STAGE1B DONE ==="
