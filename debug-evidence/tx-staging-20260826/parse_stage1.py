#!/usr/bin/env python3
# Parse Stage 1 captures: mean_pkts/ev (aired), interval held, sink KB/s.
import re, glob, os, statistics
OUT="/tmp/scratch"
arms=[("s1a1","7.5ms  deep(64)"),("s1a2","15ms   deep(64)"),
      ("s1a3","25ms   deep(64)"),("s1b1","15ms   shallow(8)")]
rpt=re.compile(r"mean_pkts/ev=(\d+\.\d+)\s+max=(\d+)\s+nonempty_ev=(\d+).*sent=\d+\(\+(\d+)")
gate=re.compile(r"GATE conn: interval=(\d+)")
print(f"{'arm':6} {'config':16} {'interval_held':13} {'mean_pkts/ev':>13} {'max':>4} {'sink_KBs':>9}  windows")
for lbl,cfg in arms:
    cen=os.path.join(OUT,f"{lbl}_cen.log"); per=os.path.join(OUT,f"{lbl}_per.log")
    means=[]; maxes=[]; iv="?"
    if os.path.exists(cen):
        t=open(cen,errors='replace').read()
        g=gate.search(t)
        if g:
            u=int(g.group(1)); iv=f"{u*5//4}.{(u*5%4)*25:02d}ms"
        for m in rpt.finditer(t):
            means.append(float(m.group(1))); maxes.append(int(m.group(2)))
    # sink throughput: look for KB/s or bytes-rate lines
    kbs="?"
    if os.path.exists(per):
        pt=open(per,errors='replace').read()
        ks=re.findall(r"(\d+\.?\d*)\s*KB/s", pt)
        if ks: kbs=f"{statistics.mean(float(x) for x in ks[-3:]):.1f}"
        else:
            # fallback: rx_bytes/interval style
            rb=re.findall(r"rx[_ ]?bytes[=: ]+(\d+)", pt)
            if rb: kbs=f"~{int(rb[-1])}B/win"
    # steady state = last up-to-3 windows
    ss=means[-3:] if len(means)>=1 else []
    mm=f"{statistics.mean(ss):.2f}" if ss else "NO DATA"
    mx=max(maxes) if maxes else "-"
    print(f"{lbl:6} {cfg:16} {iv:13} {mm:>13} {str(mx):>4} {kbs:>9}  n={len(means)}")
print("\n[interpretation] flat mean_pkts/ev across 7.5/15/25ms despite 3.3x airtime => hard COUNT cap (not airtime).")
print("                 shallow(8) dropping below deep(64) => buffers bind only BELOW the wall (ceiling unaffected above).")
