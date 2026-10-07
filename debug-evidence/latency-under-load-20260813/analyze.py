#!/usr/bin/env python3
import re, sys
path = sys.argv[1]
lines = open(path, errors='replace').read().splitlines()

# stage markers: "<ts>  LOADRAMP: target=<r> KBps"
stages = []  # (ts, rate)
for ln in lines:
    m = re.search(r'^\s*([\d.]+)\s+LOADRAMP: target=(\d+) KBps', ln)
    if m: stages.append((float(m.group(1)), int(m.group(2))))

# RTT report lines
rtt = []  # dict per line
rx = re.compile(r'^\s*([\d.]+)\s+t=(\d+)s RTT mean=(\d+) min=(\d+) max=(\d+) n=(\d+) to=(\d+).*'
                r'tot_n=(\d+) tot_to=(\d+) maxever=(\d+) >2ms=(\d+) >5ms=(\d+) >15ms=(\d+) >20ms=(\d+) >30ms=(\d+)')
for ln in lines:
    m = rx.search(ln)
    if not m: continue
    ts,dev,mean,mn,mx,n,to,tot_n,tot_to,maxever,g2,g5,g15,g20,g30 = map(lambda x:float(x) if '.' in x else int(x), m.groups())
    rtt.append(dict(ts=ts,mean=mean,mx=mx,n=n,to=to,tot_n=tot_n,tot_to=tot_to,maxever=maxever,g15=g15,g20=g20,g30=g30))

# assign each rtt line to the stage whose marker ts precedes it
def stage_of(ts):
    r=None
    for st,rate in stages:
        if ts>=st: r=rate
        else: break
    return r

# group
from collections import defaultdict
groups=defaultdict(list)
# only use the FIRST cycle stages (skip the wrap): take stages in order, first 7
seen_order=[]
for i,(st,rate) in enumerate(stages[:8]):
    end = stages[i+1][0] if i+1<len(stages) else 1e9
    seg=[r for r in rtt if st<=r['ts']<end]
    if not seg: continue
    # drop the first line of each stage (transition second) for cleaner steady-state
    seg2 = seg[1:] if len(seg)>2 else seg
    # windowed stats
    nw=sum(r['n'] for r in seg2)
    meanw=sum(r['mean']*r['n'] for r in seg2)/nw if nw else 0
    maxw=max((r['mx'] for r in seg2), default=0)
    tos=sum(r['to'] for r in seg2)
    # cumulative deltas across the stage (use tot_n/g15/... first vs last of seg)
    dn = seg[-1]['tot_n']-seg[0]['tot_n']
    d15=seg[-1]['g15']-seg[0]['g15']; d20=seg[-1]['g20']-seg[0]['g20']; d30=seg[-1]['g30']-seg[0]['g30']
    maxev=max(r['maxever'] for r in seg)
    print(f"load={rate:3d} KB/s | mean={meanw/1000:5.1f}ms  win_max={maxw/1000:5.1f}ms  "
          f"stage_maxever={maxev/1000:5.1f}ms | n={dn:5d} to={tos:2d} | "
          f">15ms={d15:4d}({100*d15/dn if dn else 0:4.1f}%) >20ms={d20:4d}({100*d20/dn if dn else 0:4.1f}%) "
          f">30ms={d30:4d}({100*d30/dn if dn else 0:4.1f}%)")
