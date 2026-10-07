#!/usr/bin/env python3
# CoC receiver-delivered goodput from the SINK cum_total byte counter.
# steady-window least-squares slope => KB/s (1KB=1024B). Same method as the GATT rig.
import re, sys
def rate(path):
    ts, cum, fsu = [], [], set()
    for ln in open(path, errors='ignore'):
        m = re.search(r'^\s*([\d.]+)\s+SINK rx:.*?fsu=(\d+).*?cum_total=(\d+)', ln)
        if m:
            ts.append(float(m.group(1))); fsu.add(int(m.group(2))); cum.append(int(m.group(3)))
    if len(cum) < 15: return None
    lo = len(cum)//4              # drop warm-up (connect/PHY/DLE/L2CAP/credit ramp)
    ts, cum = ts[lo:-2], cum[lo:-2]
    n=len(ts); sx=sum(ts); sy=sum(cum); sxx=sum(t*t for t in ts); sxy=sum(t*c for t,c in zip(ts,cum))
    slope=(n*sxy-sx*sy)/(n*sxx-sx*sx)   # bytes/s
    return slope/1024.0, ts[-1]-ts[0], n, sorted(fsu)
for p in sys.argv[1:]:
    r=rate(p)
    if r: print(f"{p.split('/')[-1]:22s} {r[0]:6.1f} KB/s  steady {r[1]:4.1f}s n={r[2]:3d} fsu={r[3]}")
    else: print(f"{p.split('/')[-1]}: insufficient data")
