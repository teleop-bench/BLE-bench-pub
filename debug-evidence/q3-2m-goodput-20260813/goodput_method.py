#!/usr/bin/env python3
# Receiver-delivered goodput from the periph Q2EVT blk= cumulative byte counter.
# Steady-window slope (least-squares) => KB/s (1KB=1024B). Same method both arms.
import re, sys
def rate(path):
    ts, bs = [], []
    for ln in open(path, errors='ignore'):
        m = re.search(r't=(\d+)ms .*?blk=(\d+)', ln)
        if m:
            ts.append(int(m.group(1))); bs.append(int(m.group(2)))
    if len(bs) < 20: return None
    # steady window: drop first 25% (connect/settle/MTU/DLE ramp) and last 3 samples
    lo = len(bs)//4
    ts, bs = ts[lo:-3], bs[lo:-3]
    # least-squares slope bytes/ms
    n = len(ts); sx=sum(ts); sy=sum(bs); sxx=sum(t*t for t in ts); sxy=sum(t*b for t,b in zip(ts,bs))
    slope = (n*sxy - sx*sy)/(n*sxx - sx*sx)   # bytes/ms
    kbps = slope*1000/1024.0
    span_s = (ts[-1]-ts[0])/1000.0
    return kbps, span_s, n, bs[-1]-bs[0]
for p in sys.argv[1:]:
    r = rate(p)
    if r: print(f"{p}: {r[0]:.1f} KB/s  (steady {r[1]:.1f}s, n={r[2]}, {r[3]} bytes)")
    else: print(f"{p}: insufficient data")
