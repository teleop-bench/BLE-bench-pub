#!/bin/zsh
set +e
R=$HOME/Desktop/develop/zenoh-pico-ble-test
CAP=$R/debug-evidence/latency-under-load-20260813/capture-tool.py
S=/tmp/scratch
O=/tmp/rematrix
CEN=/dev/cu.usbmodem0010577948573; CEND=1057794857
PER=/dev/cu.usbmodem0010577195093; PERD=1057719509
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null
FD=$O/sb-p-gsink-open  # runners.yaml donor
: > $O/rerun2.log
# NOTE: reuses built hexes; p-csink-open.hex was rebuilt with the FIXED (CONN_INTERVAL_LOW_LATENCY) sink.
sha=$(shasum -a256 $O/p-csink-open.hex | cut -c1-12); echo "fixed open CoC sink: $sha" >> $O/rerun2.log
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
CSV=$O/matrix4.csv; echo "transport,duplex,stack,fsu,interval_ms,tput_kbps" > $CSV
declare -A MS; MS[6]=7.5;MS[12]=15;MS[20]=25;MS[30]=37.5;MS[40]=50;MS[60]=75;MS[80]=100
echo "$(date +%H:%M) === RERUN (boards ~2ft) ===" >>$O/rerun2.log
for u in 6 12 20 30 40 60 80; do m=$MS[$u]
 for stk in open sdc; do for fsu in fsu off; do
   flashrun m4-g1-$stk-$fsu-$u $O/gatt-$stk-$fsu-$u.hex $O/p-gsink-$stk.hex 50
   echo "GATT,oneway,$stk,$fsu,$m,$(p_rx $S/m4-g1-$stk-$fsu-$u-per.log)" >> $CSV
   flashrun m4-gd-$stk-$fsu-$u $O/gatt-$stk-$fsu-$u.hex $O/p-gecho-$stk.hex 50
   echo "GATT,duplex,$stk,$fsu,$m,$(p_dx $S/m4-gd-$stk-$fsu-$u-cen.log $S/m4-gd-$stk-$fsu-$u-per.log)" >> $CSV
   flashrun m4-c1-$stk-$fsu-$u $O/coc-$stk-$fsu-$u.hex $O/p-csink-$stk.hex 50
   echo "CoC,oneway,$stk,$fsu,$m,$(p_coc $S/m4-c1-$stk-$fsu-$u-per.log)" >> $CSV
 done; done
 echo "$(date +%H:%M) interval $m done" >> $O/rerun2.log
done
echo "$(date +%H:%M) RERUN2_DONE: $(($(wc -l < $CSV)-1)) cells" >> $O/rerun2.log
