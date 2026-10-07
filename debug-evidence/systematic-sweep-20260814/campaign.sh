#!/bin/zsh
set +e
S=/tmp/scratch
CAP=$HOME/Desktop/develop/zenoh-pico-ble-test/debug-evidence/latency-under-load-20260813/capture-tool.py
OUT=/tmp/sweep; CSV=$OUT/results.csv
CENP=/dev/cu.usbmodem0010577948573; PERP=/dev/cu.usbmodem0010577195093
CEND=1057794857; PERD=1057719509
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null
echo "transport,duplex,controller,fsu,interval_ms,metric,value" > $CSV
# ---------- PHASE 1: BUILD ----------
zsh /tmp/scratch/sweep_build.sh >> $OUT/build.log 2>&1
FLASHDIR=/tmp/sb-p-gsink-open
echo "=== BUILDS DONE: $(ls $OUT/*.hex 2>/dev/null | wc -l) hexes; starting runs ===" >> $OUT/campaign.log
# ---------- helpers ----------
flashrun(){ # $1=label $2=chex $3=phex $4=dur
  west flash -d $FLASHDIR --no-rebuild --hex-file $3 --dev-id $PERD >/dev/null 2>&1
  west flash -d $FLASHDIR --no-rebuild --hex-file $2 --dev-id $CEND >/dev/null 2>&1
  python3 $CAP $4 ${CENP}:$1-cen ${PERP}:$1-per >/dev/null 2>&1
}
p_coc(){ python3 -c "import re,sys
T=[];C=[]
for ln in open(sys.argv[1],errors='ignore'):
 m=re.search(r'^\s*([\d.]+)\s+SINK rx:.*?cum_total=(\d+)',ln)
 if m:T.append(float(m.group(1)));C.append(int(m.group(2)))
print('' if len(C)<12 else round((lambda t,c:(len(t)*sum(a*b for a,b in zip(t,c))-sum(t)*sum(c))/(len(t)*sum(a*a for a in t)-sum(t)**2)/1024.0)(T[len(T)//4:-2],C[len(C)//4:-2]),1))" $1; }
p_rx(){ python3 -c "import re,sys;v=[int(m.group(2)) for ln in open(sys.argv[1],errors='ignore') for m in [re.search(r'P t=(\d+)s rxkBps=(\d+)',ln)] if m and int(m.group(1))>=15 and int(m.group(2))>0];print('' if len(v)<4 else sorted(v)[len(v)//2])" $1; }
p_dx(){ python3 -c "import re,sys
u=[int(m.group(1)) for ln in open(sys.argv[1],errors='ignore') for m in [re.search(r'exkBps=(\d+)',ln)] if m and int(m.group(1))>0]
d=[int(m.group(2)) for ln in open(sys.argv[2],errors='ignore') for m in [re.search(r'P t=(\d+)s rxkBps=(\d+)',ln)] if m and int(m.group(1))>=15 and int(m.group(2))>0]
u=u[len(u)//3:];print('' if len(u)<4 or len(d)<4 else sorted(u)[len(u)//2]+sorted(d)[len(d)//2])" $1 $2; }
p_rtt(){ python3 -c "import re,sys;v=[int(m.group(1)) for ln in open(sys.argv[1],errors='ignore') for m in [re.search(r'RTT mean=(\d+) min=\d+ max=\d+ n=(\d+)',ln)] if m and int(m.group(2))>0];print('' if len(v)<5 else round(sum(v[len(v)//3:])/len(v[len(v)//3:])/1000.0,1))" $1; }
# ---------- PHASE 2: RUN ----------
declare -A MS; MS[6]=7.5;MS[12]=15;MS[20]=25;MS[30]=37.5;MS[40]=50;MS[60]=75;MS[80]=100
for u in 6 12 20 30 40 60 80; do
 m=$MS[$u]
 for ctrl in open sdc; do
  for fsu in fsu off; do
   # GATT one-way
   flashrun g1-$ctrl-$fsu-$u $OUT/gatt-$ctrl-$fsu-$u.hex $OUT/p-gsink-$ctrl.hex 50
   v=$(p_rx $S/g1-$ctrl-$fsu-$u-per.log); echo "GATT,oneway,$ctrl,$fsu,$m,tput,$v" >> $CSV
   # GATT duplex
   flashrun gd-$ctrl-$fsu-$u $OUT/gatt-$ctrl-$fsu-$u.hex $OUT/p-gecho-$ctrl.hex 50
   v=$(p_dx $S/gd-$ctrl-$fsu-$u-cen.log $S/gd-$ctrl-$fsu-$u-per.log); echo "GATT,duplex,$ctrl,$fsu,$m,tput,$v" >> $CSV
   # CoC one-way
   flashrun c1-$ctrl-$fsu-$u $OUT/coc-$ctrl-$fsu-$u.hex $OUT/p-csink-$ctrl.hex 50
   v=$(p_coc $S/c1-$ctrl-$fsu-$u-per.log); echo "CoC,oneway,$ctrl,$fsu,$m,tput,$v" >> $CSV
  done
  # latency (fsu-off, ping mode)
  flashrun lat-$ctrl-$u $OUT/lat-$ctrl-$u.hex $OUT/p-gecho-$ctrl.hex 45
  v=$(p_rtt $S/lat-$ctrl-$u-cen.log); echo "-,-,$ctrl,-,$m,rtt_ms,$v" >> $CSV
 done
 echo "interval $m ms done ($(date))" >> $OUT/campaign.log
done
echo "=== CAMPAIGN COMPLETE: $(wc -l < $CSV) rows ===" >> $OUT/campaign.log
echo CAMPAIGN_DONE
