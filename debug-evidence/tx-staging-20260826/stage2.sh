#!/bin/bash
# Stage 2: host-handed PDU counter (l2cap_pull_pdus, BT_TESTING).
#  A) downlink sanity: central host_pulls/ev vs aired mean_pkts/ev @15ms deep.
#  B) peripheral DLE=251 STALL: does peripheral host_pulls stay ~0 (host never stages)
#     while up_sent climbs then freezes + txcred frozen? => stall is host-side.
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
LOG=$OUT/stage2-driver.log; : > $LOG
say(){ echo "$*" | tee -a $LOG; }
bld(){ local d=$1 app=$2; shift 2
  west build -p always -b $B -d /private/tmp/$d $app -- "$@" >$OUT/build-$d.log 2>&1 \
    && say "BUILT $d" || { say "FAIL $d"; grep -E "error:" $OUT/build-$d.log|head -3|tee -a $LOG; }
}
say "=== builds (BT_TESTING) ==="
bld s2-cen  coc-central -DCONFIG_BT_TESTING=y -DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y \
    -DCONFIG_APP_SDU_SIZE=480 -DCONFIG_APP_CONN_INT_UNITS=12 \
    -DCONFIG_BT_BUF_ACL_TX_COUNT=64 -DCONFIG_BT_CONN_TX_MAX=64
bld s2-snk  coc-sink -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n
bld s2-dxc  coc-duplex-central -DCONFIG_BT_TESTING=y
bld s2-dxs  coc-duplex-sink    -DCONFIG_BT_TESTING=y

# Run A: downlink sanity
say "=== RUN A: downlink host_pulls vs aired @15ms deep ==="
west flash -d /private/tmp/s2-snk --dev-id $PER --no-rebuild 2>&1|tail -1|tee -a $LOG
west flash -d /private/tmp/s2-cen --dev-id $CEN --no-rebuild 2>&1|tail -1|tee -a $LOG
python3 $CAP 42 $CENP:s2a_cen $PERP:s2a_per >/dev/null 2>&1 & C=$!
sleep 6; nrfutil device reset --serial-number $PER >/dev/null 2>&1
sleep 3; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
wait $C; say "captured s2a"

# Run B: peripheral DLE=251 stall
say "=== RUN B: peripheral DLE=251 uplink STALL (host_pulls?) ==="
west flash -d /private/tmp/s2-dxs --dev-id $PER --no-rebuild 2>&1|tail -1|tee -a $LOG
west flash -d /private/tmp/s2-dxc --dev-id $CEN --no-rebuild 2>&1|tail -1|tee -a $LOG
python3 $CAP 45 $CENP:s2b_cen $PERP:s2b_per >/dev/null 2>&1 & C=$!
sleep 6; nrfutil device reset --serial-number $PER >/dev/null 2>&1
sleep 3; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
wait $C; say "captured s2b"
say "=== STAGE2 DONE ==="
