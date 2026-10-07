#!/bin/bash
# EATT publication-grade rerun. Firmware: matching-seq wake + per-probe k_sem_reset; counter renamed
# late_pong. PREREGISTERED corrected gate (checked automatically): (a) no late_pong before the first
# timeout, (b) cumulative late_pong <= cumulative tot_to, for ALL four arms. Timeout-inclusive metrics.
# ABBA (ON/OFF/OFF/ON), reset-isolated. Archives BOTH endpoints' hexes+SHA+config+logs.
set -u
source $HOME/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
cd $HOME/Desktop/develop/zenoh-pico-ble-test
B=nrf54l15dk/nrf54l15/cpuapp
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
CEN=1057794857; PER=1057719509
CENP=/dev/cu.usbmodem0010577948573; PERP=/dev/cu.usbmodem0010577195093
OUT=/tmp/scratch
export CAP_OUTDIR=$OUT
LOG=$OUT/eatt-rerun2.log; : > $LOG
say(){ echo "$*" | tee -a $LOG; }
bld(){ local d=$1 app=$2; shift 2; west build -p always -b $B -d /private/tmp/$d $app -- "$@" >$OUT/build-$d.log 2>&1 \
  && say "BUILT $d" || { say "FAIL $d"; grep -E "error:" $OUT/build-$d.log|head -3|tee -a $LOG; }; }
OV="loadramp.conf;gdeep.conf"
say "=== rebuild (renamed counter + matching-seq fix) ==="
bld p2on-cen  eattlat-central -DEXTRA_CONF_FILE="$OV;eatt.conf"     -DCONFIG_APP_CONN_INT_UNITS=12
bld p2on-per  eattlat-periph  -DEXTRA_CONF_FILE="$OV;eatt.conf"
bld p2off-cen eattlat-central -DEXTRA_CONF_FILE="$OV;eatt-off.conf" -DCONFIG_APP_CONN_INT_UNITS=12
bld p2off-per eattlat-periph  -DEXTRA_CONF_FILE="$OV;eatt-off.conf"
for h in p2on-cen p2on-per p2off-cen p2off-per; do [ -f /private/tmp/$h/zephyr/zephyr.hex ] || { say "BUILD INCOMPLETE"; exit 1; }; done
runcap(){ local cdir=$1 pdir=$2 lbl=$3 secs=$4
  west flash -d /private/tmp/$pdir --dev-id $PER --no-rebuild 2>&1|tail -1|tee -a $LOG
  west flash -d /private/tmp/$cdir --dev-id $CEN --no-rebuild 2>&1|tail -1|tee -a $LOG
  python3 $CAP $secs $CENP:${lbl}_cen $PERP:${lbl}_per >/dev/null 2>&1 & local C=$!
  sleep 6; nrfutil device reset --serial-number $PER >/dev/null 2>&1
  sleep 3; nrfutil device reset --serial-number $CEN >/dev/null 2>&1
  wait $C; say "captured $lbl"; }
say "=== smoke: eatt>=2 ==="
runcap p2on-cen p2on-per p2_smoke 45
EB=$(grep -oE "eatt=[0-9]+" $OUT/p2_smoke_cen.log 2>/dev/null|grep -oE "[0-9]+"|sort -rn|head -1)
say "smoke eatt=${EB:-?}"; { [ -n "$EB" ] && [ "$EB" -ge 2 ]; } || { say "⛔ smoke eatt<2 STOP"; say "=== DONE (gated) ==="; exit 0; }
say "=== ABBA ramps ==="
runcap p2on-cen  p2on-per  p2_on1  455
runcap p2off-cen p2off-per p2_off1 455
runcap p2off-cen p2off-per p2_off2 455
runcap p2on-cen  p2on-per  p2_on2  455

say "=== PREREGISTERED GATE + timeout-inclusive analysis ==="
python3 - "$OUT" <<'PY' | tee -a $LOG
import re,sys,statistics
O=sys.argv[1]
rr=re.compile(r"late_pong=(\d+) .*tot_to=(\d+)")
pctl=re.compile(r"PCTL target=(\d+) tot=(\d+) to=(\d+) p50=(\d+) .*g30=(\d+) g100=(\d+)")
arms={'ON':['p2_on1','p2_on2'],'OFF':['p2_off1','p2_off2']}
ok=True
for arm,fs in arms.items():
  for f in fs:
    lp_before_to=0; lp_final=0; to_final=0; lp_gt_to=0   # (b) checked PER LINE, not just final
    for line in open(f"{O}/{f}_cen.log",errors='replace'):
      m=rr.search(line)
      if m:
        lp,to=int(m.group(1)),int(m.group(2))
        if to==0 and lp>0: lp_before_to=max(lp_before_to,lp)
        if lp>to: lp_gt_to+=1                             # any line where cumulative late_pong exceeds timeouts
        lp_final=lp; to_final=to
    a = (lp_before_to==0); b = (lp_gt_to==0)              # (b): late_pong<=tot_to on EVERY line (throughout)
    print(f"GATE {f}: (a)no-late-pong-before-timeout={'PASS' if a else 'FAIL('+str(lp_before_to)+')'}  (b)late_pong<=timeouts on every line={'PASS' if b else 'FAIL('+str(lp_gt_to)+' lines)'} [final {lp_final}<={to_final}]")
    ok = ok and a and b
print("PREREGISTERED GATE:", "PASS — VALID" if ok else "FAIL — INVALID")
# timeout-inclusive pooled table
data={}
for arm,fs in arms.items():
  for f in fs:
    for line in open(f"{O}/{f}_cen.log",errors='replace'):
      m=pctl.search(line)
      if m:
        t,tot,to,p50,g30,g100=map(int,m.group(1,2,3,4,5,6))
        d=data.setdefault((arm,t),[0,0,0,0,[]]); d[0]+=tot;d[1]+=to;d[2]+=g30;d[3]+=g100;d[4].append(p50)
print(f"\n{'load':>5} | {'ON>30+to':>8} {'OFF>30+to':>9} | {'ON>100+to':>9} {'OFF>100+to':>10} | {'ONmed':>5} {'OFFmed':>6} | {'ONto%':>5} {'OFFto%':>6}")
for t in [0,25,50,75,100,125,150]:
  o=data.get(('ON',t)); f=data.get(('OFF',t))
  if not o or not f: continue
  v=lambda d,g:( (d[2]+d[1]) if g=='30' else (d[3]+d[1]) )/(d[0]+d[1])*100
  md=lambda d: statistics.median(d[4]); tr=lambda d: d[1]/(d[0]+d[1])*100
  print(f"{t:>5} | {v(o,'30'):>7.1f}% {v(f,'30'):>8.1f}% | {v(o,'100'):>8.1f}% {v(f,'100'):>9.1f}% | {md(o):>5.0f} {md(f):>6.0f} | {tr(o):>4.1f}% {tr(f):>5.1f}%")
PY
say "=== EATT-RERUN2 DONE ==="
