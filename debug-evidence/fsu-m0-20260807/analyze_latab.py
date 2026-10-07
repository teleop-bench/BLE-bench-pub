#!/usr/bin/env python3
"""2M/52 latency A/B analyzer rev 2 — amended per external review WHILE
the m0-latab series was still capturing, before any log was read.

DESIGN: same FSU-enabled build family; arm A requests [150..150]
(no-change control), arm B requests [52..150] (negotiates 52). AB BA BA
AB, 8 x 150 s cells, boards reset per cell. The known no-change-event
deviation means A emits NO completion event — therefore A's control
procedure is POSITIVELY verified by its request line instead.

PRIMARY: per-cell median of per-second RTT MEANS (t>=30 s); paired
B-A deltas; mean with 95% paired t-CI (t: n=4->3.182, n=3->4.303,
n=2->12.706). SECONDARY (registered): same analysis on the median of
per-second RTT MINS. Order effects: mean(AB pairs) vs mean(BA pairs).

VERDICT RULES (registered):
- primary CI entirely above 0: positive scheduling association detected.
- primary CI entirely inside the EQUIVALENCE BAND +/-150 us: no
  practically meaningful effect detected.
- anything else: INCONCLUSIVE; soak mechanism unresolved.
- A nonsignificant CI crossing zero does NOT establish the null.

CELL VALIDITY (any failure on an accepted cell INVALIDATES the verdict,
not merely flags): A-arm 'FSU: request [150..150] us' + 'rc=0' present;
B-arm request line + 'spacing=52 us' completion; 'PHY tx=2 rx=2'
present; zero post-settle disc and timeout deltas; no
FATAL/USAGE FAULT/Halting either log; no unexpected FSU events;
cancels final == 0 (2M/7.5 ms expectation); accepted rows >= 100.
Resolved .configs archived (latab-*.config).
"""
import re, os, glob, math

EV = os.path.dirname(os.path.abspath(__file__))
TCRIT = {1: 12.706, 2: 4.303, 3: 3.182}
EQUIV_US = 150

def cell(tag, arm):
    c = open(f'{EV}/{tag}-central.log','rb').read().decode('utf-8','replace')
    p = open(f'{EV}/{tag}-periph.log','rb').read().decode('utf-8','replace')
    invalid=[]
    rows=[(int(m.group(1)),int(m.group(2)),int(m.group(3)),int(m.group(4)),int(m.group(5)))
          for m in re.finditer(r't=(\d+)s RTT mean=(\d+) min=(\d+) max=\d+ n=(\d+) to=(\d+)',c)]
    rows=[r for r in rows if r[3]>0 and r[0]>=30]
    med=smin=None
    if len(rows)<100: invalid.append(f'rows={len(rows)}<100')
    if rows:
        means=sorted(r[1] for r in rows); med=means[len(means)//2]
        mins=sorted(r[2] for r in rows); smin=mins[len(mins)//2]
        if rows[-1][4]-rows[0][4]!=0: invalid.append('timeouts')
    if arm==150:
        if not re.search(r'FSU: request \[150\.\.150\] us[^\n]*rc=0', c):
            invalid.append('A-request-not-verified')
        if 'FSU: updated' in c: invalid.append('A-unexpected-event')
    else:
        if not re.search(r'FSU: request \[52\.\.150\] us[^\n]*rc=0', c):
            invalid.append('B-request-not-verified')
        if not re.search(r'FSU: updated status=0x00 spacing=52 us', c):
            invalid.append('B-no-completion')
    if 'PHY tx=2 rx=2' not in c: invalid.append('no-2M-confirm')
    dr=[(int(m.group(1)),int(m.group(2))) for m in re.finditer(r't=(\d+)s RTT[^\n]*disc=(\d+)\(',c)]
    dd=[d for t,d in dr if t>=30]
    if dd and dd[-1]-dd[0]!=0: invalid.append('disc')
    for mk in ('FATAL','USAGE FAULT','Halting'):
        if mk in c or mk in p: invalid.append(f'marker:{mk}')
    canc=[int(m.group(1)) for m in re.finditer(r'cancels=(\d+)\(',c)]
    if not canc or canc[-1]!=0: invalid.append(f'cancels={canc[-1] if canc else None}')
    return med,smin,invalid,len(rows)

def paired(vals, label):
    n=len(vals)
    if n<2:
        print(f'{label}: <2 pairs, no CI'); return None
    mean=sum(vals)/n
    sd=math.sqrt(sum((d-mean)**2 for d in vals)/(n-1))
    half=TCRIT[n-1]*sd/math.sqrt(n)
    print(f'{label}: mean {mean:+.0f} us  95% CI [{mean-half:+.0f}, {mean+half:+.0f}] (n={n}, t={TCRIT[n-1]})')
    return mean-half, mean+half

def main():
    seq=[]
    for t in sorted(glob.glob(f'{EV}/m0-latab-f*-central.log')):
        tag=os.path.basename(t)[:-12]
        m=re.match(r'm0-latab-f(\d+)-t(\d+)',tag)
        seq.append((int(m.group(2)),int(m.group(1)),tag))
    seq.sort()
    cells=[]; any_invalid=False
    for ts,v,tag in seq:
        med,smin,invalid,n=cell(tag,v)
        if invalid: any_invalid=True
        print(f'{tag}: arm={v} med-of-means={med} med-of-mins={smin} n={n} '
              f'{"INVALID:"+";".join(invalid) if invalid else "valid"}')
        cells.append((v,med,smin))
    dm=[];dn=[];orders=[]
    i=0
    while i+1<len(cells):
        (v1,m1,s1),(v2,m2,s2)=cells[i],cells[i+1]
        if {v1,v2}=={150,52} and m1 and m2:
            a,b=(m1,m2) if v1==150 else (m2,m1)
            sa,sb=(s1,s2) if v1==150 else (s2,s1)
            dm.append(b-a); dn.append(sb-sa); orders.append('AB' if v1==150 else 'BA')
            print(f'pair {len(dm)} [{orders[-1]}]: means A {a} -> B {b} ({b-a:+d} us); mins A {sa} -> B {sb} ({sb-sa:+d} us)')
            i+=2
        else: i+=1
    ci=paired(dm,'PRIMARY (median-of-means B-A)')
    paired(dn,'SECONDARY (median-of-mins B-A)')
    ab=[d for d,o in zip(dm,orders) if o=='AB']; ba=[d for d,o in zip(dm,orders) if o=='BA']
    if ab and ba:
        print(f'order effect (means): AB {sum(ab)/len(ab):+.0f} us vs BA {sum(ba)/len(ba):+.0f} us')
    if any_invalid:
        print('VERDICT: INVALID — one or more cells failed validity checks')
    elif ci:
        lo,hi=ci
        if lo>0: print('VERDICT: positive scheduling association detected')
        elif -EQUIV_US<lo and hi<EQUIV_US:
            print(f'VERDICT: no practically meaningful effect detected (CI inside +/-{EQUIV_US} us equivalence band)')
        else: print('VERDICT: INCONCLUSIVE; soak mechanism unresolved')

if __name__=='__main__':
    main()
