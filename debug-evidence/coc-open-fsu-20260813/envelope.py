#!/usr/bin/env python3
# Build the throughput-vs-interval envelope from the sweep sink logs.
import re, sys, glob, os
S = sys.argv[1] if len(sys.argv) > 1 else "."
def rate(path):
    ts, cum = [], []
    for ln in open(path, errors='ignore'):
        m = re.search(r'^\s*([\d.]+)\s+SINK rx:.*?cum_total=(\d+)', ln)
        if m: ts.append(float(m.group(1))); cum.append(int(m.group(2)))
    if len(cum) < 15: return None
    lo=len(cum)//4; ts,cum=ts[lo:-2],cum[lo:-2]
    n=len(ts); sx=sum(ts); sy=sum(cum); sxx=sum(t*t for t in ts); sxy=sum(t*c for t,c in zip(ts,cum))
    return (n*sxy-sx*sy)/(n*sxx-sx*sx)/1024.0
def occ(cenpath):
    best=""
    for ln in open(cenpath, errors='ignore'):
        m=re.search(r'mean_pkts/ev=([\d.]+) max=(\d+)', ln)
        if m: best=f"{m.group(1)}/{m.group(2)}"
    return best or "?"
units={6:7.5,8:10,10:12.5,12:15,16:20,24:30}
print(f"{'interval':>9} {'no-FSU':>8} {'FSU':>8} {'gain':>7}  {'pkts/ev off→on':>16}")
for U in [6,8,10,12,16,24]:
    off=glob.glob(f"{S}/sw{U}-f150-sink.log"); on=glob.glob(f"{S}/sw{U}-f52-sink.log")
    if not off or not on: continue
    ro=rate(off[0]); rn=rate(on[0])
    if ro is None or rn is None: print(f"{units[U]:>7}ms  incomplete"); continue
    oc_off=occ(f"{S}/sw{U}-f150-cen.log"); oc_on=occ(f"{S}/sw{U}-f52-cen.log")
    g=(rn-ro)/ro*100
    print(f"{units[U]:>7}ms {ro:>7.1f} {rn:>7.1f} {g:>+6.1f}%  {oc_off:>7} → {oc_on:<7}")
