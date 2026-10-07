#!/usr/bin/env python3
# duplex aggregate = uplink (central exkBps) + downlink (periph rxkBps), steady median.
import re, sys
def med(vals): 
    vals=sorted(vals); return vals[len(vals)//2] if vals else 0
def up(cen):
    v=[int(m.group(1)) for ln in open(cen,errors='ignore') for m in [re.search(r'exkBps=(\d+)',ln)] if m and int(m.group(1))>0]
    return v[len(v)//3:] if len(v)>6 else v
def down(per):
    v=[int(m.group(2)) for ln in open(per,errors='ignore') for m in [re.search(r'P t=(\d+)s rxkBps=(\d+)',ln)] if m and int(m.group(1))>=15 and int(m.group(2))>0]
    return v
for lbl,cen,per in [(a[0],a[1],a[2]) for a in [x.split(',') for x in sys.argv[1:]]]:
    u=up(cen); d=down(per); mu=med(u); md_=med(d)
    print(f"{lbl:8s} uplink={mu:4d} + downlink={md_:4d} = AGG {mu+md_:4d} kBps  (n_up={len(u)} n_dn={len(d)})")
