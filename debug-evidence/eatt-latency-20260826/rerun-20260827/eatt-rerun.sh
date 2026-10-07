#!/bin/bash
# EATT rerun — FIXED firmware (notify_cb wakes only on matching seq + per-probe k_sem_reset).
# ABBA counterbalanced (ON/OFF/OFF/ON, each reset-isolated) to cancel time drift. Hard gate: any
# seqbad>0 in any arm => INVALID. Rebuilds -p always so the fixed source is picked up.
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
export CAP_OUTDIR=$OUT
LOG=$OUT/eatt-rerun.log; : > $LOG
say(){ echo "$*" | tee -a $LOG; }
bld(){ local d=$1 app=$2; shift 2
  west build -p always -b $B -d /private/tmp/$d $app -- "$@" >$OUT/build-$d.log 2>&1 \
    && say "BUILT $d" || { say "FAIL $d"; grep -E "error:" $OUT/build-$d.log|head -3|tee -a $LOG; }
}
OV="loadramp.conf;gdeep.conf"
say "=== rebuild eon/eoff (FIXED source) ==="
bld reon-cen  eattlat-central -DEXTRA_CONF_FILE="$OV;eatt.conf"     -DCONFIG_APP_CONN_INT_UNITS=12
bld reon-per  eattlat-periph  -DEXTRA_CONF_FILE="$OV;eatt.conf"
bld reoff-cen eattlat-central -DEXTRA_CONF_FILE="$OV;eatt-off.conf" -DCONFIG_APP_CONN_INT_UNITS=12
bld reoff-per eattlat-periph  -DEXTRA_CONF_FILE="$OV;eatt-off.conf"
for h in reon-cen reon-per reoff-cen reoff-per; do [ -f /private/tmp/$h/zephyr/zephyr.hex ] || { say "BUILD INCOMPLETE"; exit 1; }; done

runcap(){ local cdir=$1 pdir=$2 lbl=$3 secs=$4
  west flash -d /private/tmp/$pdir --dev-id $PER --no-rebuild 2>&1|tail -1|tee -a $LOG
  west flash -d /private/tmp/$cdir --dev-id $CEN --no-rebuild 2>&1|tail -1|tee -a $LOG
  python3 $CAP $secs $CENP:${lbl}_cen $PERP:${lbl}_per >/dev/null 2>&1 & local C=$!
  sleep 6; nrfutil device reset --serial-number $PER >/dev/null 2>&1
  sleep 3; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
  wait $C; say "captured $lbl"
}

say "=== SMOKE (fixed): eon 45s, want eatt>=2 AND seqbad=0 ==="
runcap reon-cen reon-per rre_smoke 45
EB=$(grep -oE "eatt=[0-9]+" $OUT/rre_smoke_cen.log 2>/dev/null|grep -oE "[0-9]+"|sort -rn|head -1)
SB=$(grep -oE "seqbad=[0-9]+" $OUT/rre_smoke_cen.log 2>/dev/null|grep -oE "[0-9]+"|sort -rn|head -1)
say "smoke: eatt=${EB:-?} maxseqbad=${SB:-?}"
{ [ -n "$EB" ] && [ "$EB" -ge 2 ] && [ -n "$SB" ] && [ "$SB" -eq 0 ]; } || { say "⛔ smoke failed gate (need eatt>=2 & seqbad=0) — STOP"; grep -E "SECURITY|seqbad" $OUT/rre_smoke_cen.log|tail -3|tee -a $LOG; say "=== EATT-RERUN DONE (gated) ==="; exit 0; }

say "=== ABBA ramps (ON / OFF / OFF / ON), reset-isolated, 455s each ==="
runcap reon-cen  reon-per  rre_on1  455
runcap reoff-cen reoff-per rre_off1 455
runcap reoff-cen reoff-per rre_off2 455
runcap reon-cen  reon-per  rre_on2  455

say "=== VALIDITY GATE + per-stage PCTL ==="
BAD=0
for lbl in rre_on1 rre_off1 rre_off2 rre_on2; do
  SB=$(grep -oE "seqbad=[0-9]+" $OUT/${lbl}_cen.log 2>/dev/null|grep -oE "[0-9]+"|sort -rn|head -1)
  say "$lbl maxseqbad=${SB:-?}"; [ -n "$SB" ] && [ "$SB" -gt 0 ] && { say "⛔ $lbl seqbad>0 INVALID"; BAD=1; }
done
[ "$BAD" = 0 ] && say "✅ all arms seqbad=0 — VALID" || say "⛔ some arm invalid"
for lbl in rre_on1 rre_off1 rre_off2 rre_on2; do
  say "--- $lbl PCTL ---"; grep -E "PCTL target=" $OUT/${lbl}_cen.log 2>/dev/null | tail -7 | tee -a $LOG
done
say "=== EATT-RERUN DONE ==="
