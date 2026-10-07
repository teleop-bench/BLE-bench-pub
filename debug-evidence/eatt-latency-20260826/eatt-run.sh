#!/bin/bash
# EATT latency-under-load A/B: stop-signal RTT tail vs bulk load, EATT-on vs EATT-off (both encrypted).
# Gate: smoke must show eatt>=2 bearers before the (long) ramps run.
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
LOG=$OUT/eatt-run.log; : > $LOG
say(){ echo "$*" | tee -a $LOG; }
bld(){ local d=$1 app=$2; shift 2
  west build -p always -b $B -d /private/tmp/$d $app -- "$@" >$OUT/build-$d.log 2>&1 \
    && say "BUILT $d" || { say "FAIL $d"; grep -E "error:|Error|undefined" $OUT/build-$d.log|head -4|tee -a $LOG; }
}
OV="loadramp.conf;gdeep.conf"
say "=== build EATT-on + EATT-off (with eatt-count print) ==="
bld eon-cen eattlat-central -DEXTRA_CONF_FILE="$OV;eatt.conf"     -DCONFIG_APP_CONN_INT_UNITS=12
bld eon-per eattlat-periph  -DEXTRA_CONF_FILE="$OV;eatt.conf"
bld eoff-cen eattlat-central -DEXTRA_CONF_FILE="$OV;eatt-off.conf" -DCONFIG_APP_CONN_INT_UNITS=12
bld eoff-per eattlat-periph  -DEXTRA_CONF_FILE="$OV;eatt-off.conf"
for h in eon-cen eon-per eoff-cen eoff-per; do [ -f /private/tmp/$h/zephyr/zephyr.hex ] || { say "BUILD INCOMPLETE $h"; exit 1; }; done

runcap(){ local cdir=$1 pdir=$2 lbl=$3 secs=$4
  west flash -d /private/tmp/$pdir --dev-id $PER --no-rebuild 2>&1|tail -1|tee -a $LOG
  west flash -d /private/tmp/$cdir --dev-id $CEN --no-rebuild 2>&1|tail -1|tee -a $LOG
  python3 $CAP $secs $CENP:${lbl}_cen $PERP:${lbl}_per >/dev/null 2>&1 & local C=$!
  sleep 6; nrfutil device reset --serial-number $PER >/dev/null 2>&1
  sleep 3; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
  wait $C; say "captured $lbl"
}

say "=== SMOKE: EATT-on, 45s, verify eatt>=2 bearers ==="
runcap eon-cen eon-per eatt_v 45
EB=$(grep -oE "eatt=[0-9]+" $OUT/eatt_v_cen.log 2>/dev/null | grep -oE "[0-9]+" | sort -rn | head -1)
say "max eatt bearers seen: ${EB:-none}"
if [ -z "$EB" ] || [ "$EB" -lt 2 ]; then
  say "=== EATT bearers did NOT reach >=2 -> STOP (needs config debug, not running ramps) ==="
  grep -E "set_security|SECURITY|eatt=" $OUT/eatt_v_cen.log 2>/dev/null | tail -5 | tee -a $LOG
  say "=== EATT-RUN DONE (gated) ==="; exit 0
fi

say "=== FULL RAMP — EATT ON (7 stages x 60s) ==="
runcap eon-cen eon-per eatt_on 455
say "=== FULL RAMP — EATT OFF (single-bearer control) ==="
runcap eoff-cen eoff-per eatt_off 455
# HARD VALIDITY GATE: any seqbad>0 means the RTT samples timed stale/reordered pongs, not matched
# ping->pong (the 2026-08-26 defect). A valid run MUST show seqbad=0 in every arm.
for lbl in eatt_on eatt_off; do
  SB=$(grep -oE "seqbad=[0-9]+" $OUT/${lbl}_cen.log 2>/dev/null | grep -oE "[0-9]+" | sort -rn | head -1)
  say "$lbl max seqbad = ${SB:-?}"
  if [ -n "$SB" ] && [ "$SB" -gt 0 ]; then say "⛔ INVALID: $lbl has seqbad>0 -> stale-pong contamination. Discard."; fi
done
say "=== per-stage RTT dump (both arms) ==="
for lbl in eatt_on eatt_off; do
  say "--- $lbl: LOADRAMP dumps ---"
  grep -E "PCTL target=" $OUT/${lbl}_cen.log 2>/dev/null | tail -14 | tee -a $LOG
done
say "=== EATT-RUN DONE ==="
