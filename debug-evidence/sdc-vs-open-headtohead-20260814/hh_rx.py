#!/usr/bin/env python3
# steady-state rxkBps from the periph console (P t=.. rxkBps=N). Take samples after FSU settles.
import re, sys
def rate(path, tmin=22):
    vals=[]
    for ln in open(path, errors='ignore'):
        m=re.search(r'P t=(\d+)s rxkBps=(\d+)', ln)
        if m:
            t=int(m.group(1)); v=int(m.group(2))
            if t>=tmin and v>0: vals.append(v)
    if len(vals)<4: return None
    vals=sorted(vals)
    med=vals[len(vals)//2]
    return sum(vals)/len(vals), med, vals[-1], len(vals)
for p in sys.argv[1:]:
    r=rate(p); lbl=p.split('/')[-1].replace('-per.log','')
    if r: print(f"{lbl:8s} mean={r[0]:6.1f} med={r[1]:4d} max={r[2]:4d} kBps  (n={r[3]}, steady t>=22s)")
    else: print(f"{lbl}: insufficient")
