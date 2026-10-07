#!/bin/zsh
# Full throughput matrix re-run (new arrangement, single-run sweep).
# GATT one-way, GATT duplex, CoC one-way x open+SDC x FSU on/off x 7 intervals = 84 cells.
# Uses the recovered repo apps (z54-lat-central/periph, coc-central/coc-sink). Autonomous.
set +e
R=$HOME/Desktop/develop/zenoh-pico-ble-test
Z54C=$R/z54-lat-central; Z54P=$R/z54-lat-periph; COC=$R/coc-central; COCS=$R/coc-sink
CAP=$R/debug-evidence/latency-under-load-20260813/capture-tool.py
S=/tmp/scratch
OUT=/tmp/rematrix; mkdir -p $OUT
CEN=/dev/cu.usbmodem0010577948573; CEND=1057794857
PER=/dev/cu.usbmodem0010577195093; PERD=1057719509
B=nrf54l15dk/nrf54l15/cpuapp; NCS=$HOME/ncs
LOG=$OUT/sweep.log; : > $LOG; : > $OUT/build.log
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null

# ---- build helper: $1=name $2=appdir $3=open|sdc $4=extra $5=units(or -) ----
bld(){ local name=$1 app=$2 env=$3 extra=$4 u=$5 d=$OUT/sb-$1 hx
  rm -rf $d; local uarg=""; [ "$u" != "-" ] && uarg="-DCONFIG_APP_CONN_INT_UNITS=$u"
  if [ "$env" = open ]; then
    west build -p always -b $B -d $d $app -- -DEXTRA_CONF_FILE="$extra" $uarg >>$OUT/build.log 2>&1
    hx=$d/zephyr/zephyr.hex
  else
    ( cd $NCS && unset ZEPHYR_BASE && nrfutil toolchain-manager launch --ncs-version v3.4.0 -- \
      west build -p always -b $B -d $d $app -- -DEXTRA_CONF_FILE="$extra" $uarg ) >>$OUT/build.log 2>&1
    hx="$(ls $d/*/zephyr/zephyr.hex 2>/dev/null | head -1)"
  fi
  if [ -f "$hx" ]; then cp "$hx" $OUT/$name.hex; echo "$(date +%H:%M) OK  $name" >>$LOG
  else echo "$(date +%H:%M) FAIL $name" >>$LOG; fi
}

# ---- PHASE 1: BUILD ----
echo "$(date +%H:%M) === BUILD ===" >>$LOG
bld p-gsink-open $Z54P open "tput-open.conf;fsu-open.conf" -
bld p-gsink-sdc  $Z54P sdc  "sdc-sel.conf;tput.conf;sdc-6x.conf;sdc-llbuf.conf" -
bld p-gecho-open $Z54P open "tput-echo-open.conf;fsu-open.conf" -
bld p-gecho-sdc  $Z54P sdc  "sdc-sel.conf;tput-echo.conf;sdc-6x.conf;sdc-llbuf.conf" -
bld p-csink-open $COCS open "open-fsu.conf" -
bld p-csink-sdc  $COCS sdc  "sdc-sel.conf;sdc.conf" -
for u in 6 12 20 30 40 60 80; do
  bld gatt-open-fsu-$u $Z54C open "tput-open.conf;fsu-open.conf;hh-fsu52.conf" $u
  bld gatt-open-off-$u $Z54C open "tput-open.conf" $u
  bld gatt-sdc-fsu-$u  $Z54C sdc  "sdc-sel.conf;tput.conf;hh-sdc-fsu.conf" $u
  bld gatt-sdc-off-$u  $Z54C sdc  "sdc-sel.conf;tput.conf;hh-sdc-off.conf" $u
  bld coc-open-fsu-$u  $COC  open "open-fsu.conf" $u
  bld coc-open-off-$u  $COC  open "open-nofsu.conf" $u
  bld coc-sdc-fsu-$u   $COC  sdc  "sdc-sel.conf;sdc-fsu.conf" $u
  bld coc-sdc-off-$u   $COC  sdc  "sdc-sel.conf;sdc-nofsu.conf" $u
done
echo "$(date +%H:%M) BUILDS DONE: $(ls $OUT/*.hex 2>/dev/null | wc -l) hexes" >>$LOG
FD=$OUT/sb-p-gsink-open   # any valid build dir for runners.yaml

# ---- parsers ----
p_rx(){ python3 -c "import re,sys;v=[int(m.group(2)) for ln in open(sys.argv[1],errors='ignore') for m in [re.search(r'P t=(\d+)s rxkBps=(\d+)',ln)] if m and int(m.group(1))>=15 and int(m.group(2))>0];print('' if len(v)<4 else sorted(v)[len(v)//2])" $1; }
p_dx(){ python3 -c "
import re,sys
u=[int(m.group(1)) for ln in open(sys.argv[1],errors='ignore') for m in [re.search(r'exkBps=(\d+)',ln)] if m and int(m.group(1))>0]
d=[int(m.group(2)) for ln in open(sys.argv[2],errors='ignore') for m in [re.search(r'P t=(\d+)s rxkBps=(\d+)',ln)] if m and int(m.group(1))>=15 and int(m.group(2))>0]
u=u[len(u)//3:];print('' if len(u)<4 or len(d)<4 else sorted(u)[len(u)//2]+sorted(d)[len(d)//2])" $1 $2; }
p_coc(){ python3 -c "import re,sys
T=[];C=[]
for ln in open(sys.argv[1],errors='ignore'):
 m=re.search(r'^\s*([\d.]+)\s+SINK rx:.*?cum_total=(\d+)',ln)
 if m:T.append(float(m.group(1)));C.append(int(m.group(2)))
print('' if len(C)<12 else round((lambda t,c:(len(t)*sum(a*b for a,b in zip(t,c))-sum(t)*sum(c))/(len(t)*sum(a*a for a in t)-sum(t)**2)/1024.0)(T[len(T)//4:-2],C[len(C)//4:-2]),1))" $1; }
flashrun(){ west flash -d $FD --no-rebuild --hex-file $3 --dev-id $PERD >/dev/null 2>&1
  west flash -d $FD --no-rebuild --hex-file $2 --dev-id $CEND >/dev/null 2>&1
  sleep 2; python3 $CAP $4 ${CEN}:$1-cen ${PER}:$1-per >/dev/null 2>&1; }

# ---- PHASE 2: RUN SWEEP ----
CSV=$OUT/matrix.csv; echo "transport,duplex,stack,fsu,interval_ms,tput_kbps" > $CSV
declare -A MS; MS[6]=7.5;MS[12]=15;MS[20]=25;MS[30]=37.5;MS[40]=50;MS[60]=75;MS[80]=100
echo "$(date +%H:%M) === SWEEP ===" >>$LOG
for u in 6 12 20 30 40 60 80; do m=$MS[$u]
 for stk in open sdc; do for fsu in fsu off; do
   flashrun g1-$stk-$fsu-$u $OUT/gatt-$stk-$fsu-$u.hex $OUT/p-gsink-$stk.hex 50
   echo "GATT,oneway,$stk,$fsu,$m,$(p_rx $S/g1-$stk-$fsu-$u-per.log)" >> $CSV
   flashrun gd-$stk-$fsu-$u $OUT/gatt-$stk-$fsu-$u.hex $OUT/p-gecho-$stk.hex 50
   echo "GATT,duplex,$stk,$fsu,$m,$(p_dx $S/gd-$stk-$fsu-$u-cen.log $S/gd-$stk-$fsu-$u-per.log)" >> $CSV
   flashrun c1-$stk-$fsu-$u $OUT/coc-$stk-$fsu-$u.hex $OUT/p-csink-$stk.hex 50
   echo "CoC,oneway,$stk,$fsu,$m,$(p_coc $S/c1-$stk-$fsu-$u-per.log)" >> $CSV
 done; done
 echo "$(date +%H:%M) interval $m done" >> $LOG
done
echo "$(date +%H:%M) SWEEP_DONE: $(($(wc -l < $CSV)-1)) cells" >> $LOG
echo "REMATRIX_DONE" >> $LOG
