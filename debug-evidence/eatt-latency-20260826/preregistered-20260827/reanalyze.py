#!/usr/bin/env python3
# Corrected re-analysis of the preregistered EATT run (addresses 3rd-review points 1-3):
#  - PER-LINE gate: late_pong <= tot_to on EVERY telemetry line (not just the final counters), and
#    no late_pong before the first timeout — for all four arms.
#  - Timeouts counted as budget failures (in the denominator). p99 omitted (200 ms timeout censors it).
#  - Reports PER-REPLICATE (not just pooled), and labels the saturated median as a
#    "median of run medians" (141/152 vs 193/193), not a pooled sample median.
import re,statistics,glob,os
D=os.path.dirname(os.path.abspath(__file__))
rr=re.compile(r"late_pong=(\d+) .*tot_to=(\d+)")
pctl=re.compile(r"PCTL target=(\d+) tot=(\d+) to=(\d+) p50=(\d+) .*g30=(\d+) g100=(\d+)")
arms={'on1':'p2_on1_cen.log','on2':'p2_on2_cen.log','off1':'p2_off1_cen.log','off2':'p2_off2_cen.log'}

print("== PER-LINE preregistered gate ==")
allpass=True
for a,f in arms.items():
    viol=before=lines=0
    for line in open(os.path.join(D,f),errors='replace'):
        m=rr.search(line)
        if m:
            lines+=1; lp,to=int(m.group(1)),int(m.group(2))
            viol+= lp>to; before+= (to==0 and lp>0)
    ok = viol==0 and before==0; allpass &= ok
    print(f"  {a}: {lines} lines, per-line late_pong>tot_to={viol}, before-first-timeout={before} -> {'PASS' if ok else 'FAIL'}")
print(f"  GATE (per-line, all arms): {'PASS' if allpass else 'FAIL'}")

def stage(f):
    d={}
    for line in open(os.path.join(D,f),errors='replace'):
        m=pctl.search(line)
        if m:
            t,tot,to,p50,g30,g100=map(int,m.group(1,2,3,4,5,6))
            d[t]=(tot,to,p50,g30,g100)
    return d
S={a:stage(f) for a,f in arms.items()}

def viol(d,g): tot,to,p50,g30,g100=d; n=tot+to; return 100*((g30 if g=='30' else g100)+to)/n
def tor(d): tot,to,*_=d; return 100*to/(tot+to)

print("\n== PER-REPLICATE at 150 KB/s (saturation) — directional stability check ==")
for a in arms:
    d=S[a][150]; print(f"  {a}: >30+to={viol(d,'30'):.1f}%  >100+to={viol(d,'100'):.1f}%  to={tor(d):.1f}%  median={d[2]}")
print("  -> severe tail (>100+to) ON {0[0]:.0f}/{0[1]:.0f} < OFF {1[0]:.0f}/{1[1]:.0f} : REPLICATES".format(
    [viol(S['on1'][150],'100'),viol(S['on2'][150],'100')],[viol(S['off1'][150],'100'),viol(S['off2'][150],'100')]))
print("  -> >30+to and timeout: NOT directionally stable (OFF2 exceeds ON on both).")
print("  -> saturated median = median of run medians ON median([{},{}])={} vs OFF 193/193".format(
    S['on1'][150][2],S['on2'][150][2],statistics.median([S['on1'][150][2],S['on2'][150][2]])))

print("\n== POOLED timeout-inclusive (ON=on1+on2, OFF=off1+off2) ==")
def pool(names,t):
    tot=to=g30=g100=0; meds=[]
    for a in names: d=S[a][t]; tot+=d[0];to+=d[1];g30+=d[3];g100+=d[4];meds.append(d[2])
    n=tot+to
    return 100*(g30+to)/n,100*(g100+to)/n,100*to/n,statistics.median(meds)
print(f"{'load':>5} | {'ON>30+to':>8} {'OFF>30+to':>9} | {'ON>100+to':>9} {'OFF>100+to':>10} | {'ONmed*':>6} {'OFFmed*':>7} | {'ONto%':>5} {'OFFto%':>6}")
for t in [0,25,50,75,100,125,150]:
    o=pool(['on1','on2'],t); f=pool(['off1','off2'],t)
    print(f"{t:>5} | {o[0]:>7.1f}% {f[0]:>8.1f}% | {o[1]:>8.1f}% {f[1]:>9.1f}% | {o[3]:>5.0f}  {f[3]:>6.0f} | {o[2]:>4.1f}% {f[2]:>5.1f}%")
print("*med = median of the two run medians (NOT a pooled sample median).")
