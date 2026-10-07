#!/usr/bin/env python3
# parse steady-state RTT (us) from the central console; report mean/min/max in ms.
import re, sys
def stats(path):
    means=[]; mins=[]; maxs=[]; tot_n=0; tot_to=0
    for ln in open(path, errors='ignore'):
        m=re.search(r'RTT mean=(\d+) min=(\d+) max=(\d+) n=(\d+) to=(\d+)', ln)
        if m:
            mn,mi,mx,n,to=map(int,m.groups())
            if n>0: means.append(mn); mins.append(mi); maxs.append(mx); tot_n+=n; tot_to+=to
    if len(means)<5: return None
    st=means[len(means)//3:]                # drop warmup third
    return (sum(st)/len(st)/1000.0, min(mins)/1000.0, max(maxs)/1000.0, tot_n, tot_to)
for p in sys.argv[1:]:
    r=stats(p)
    lbl=p.split('/')[-1].replace('-cen.log','')
    if r: print(f"{lbl:14s} mean={r[0]:6.2f}ms  min={r[1]:5.2f}  max={r[2]:6.2f}  n={r[3]} to={r[4]}")
    else: print(f"{lbl}: insufficient RTT data")
