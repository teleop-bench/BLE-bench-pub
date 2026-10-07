#!/usr/bin/env python3
"""Phase-5 2M system-payoff A/B — registered before any m0-pay2m-* log is
read (plan Phase-5 rewrite, commit 3325f11).

DESIGN: blast+sink rig, 2M, 7.5 ms, DLE-251; same FSU-enabled build
family; A = request [150..150] (control), B = [52..150] (negotiates 52).
AB BA BA AB, 8 x 150 s cells, boards reset per cell.

INTERPRETATION (registered): this measures OBSERVED SYSTEM PAYOFF under
the demonstrated ~157 KiB/s instrument ceiling (LOCATION undetermined —
host-pump attribution retracted) and pairs-per-event quantization
masking. A null result means "no system-level throughput payoff at this
operating point under masking" and says NOTHING about radio-level
benefit. A positive result is a genuine system payoff.

PRIMARY: per-cell median per-second periph rxkBps (>10), drop first 20
kept; paired B-A deltas (%); mean with 95% t-CI (n=4 t=3.182, n=3 4.303,
n=2 12.706). SECONDARY: central BLASTC cgap_min medians (reported;
2M completion batching known — not a pacing meter here).

VALIDITY (any failure on any cell -> verdict INVALID): A request line
[150..150] rc=0 + no FSU event; B request line + spacing=52 completion;
'PHY tx=2 rx=2' both... (central log) present; no FATAL/USAGE FAULT/
Halting either log; post-settle disc delta 0; cancels final == 0
(2M/7.5 expectation); >=100 accepted samples.
"""
import re, os, glob, math
EV = os.path.dirname(os.path.abspath(__file__))
TCRIT={1:12.706,2:4.303,3:3.182}

def cell(tag, arm):
    c=open(f'{EV}/{tag}-central.log','rb').read().decode('utf-8','replace')
    p=open(f'{EV}/{tag}-periph.log','rb').read().decode('utf-8','replace')
    inv=[]
    rx=[int(m.group(1)) for m in re.finditer(r'rxkBps=(\d+)',p) if int(m.group(1))>10]
    kept=rx[20:]
    med=None
    if len(kept)<100: inv.append(f'rows={len(kept)}<100')
    if kept:
        v=sorted(kept); med=v[len(v)//2]
    if arm==150:
        if not re.search(r'FSU: request \[150\.\.150\] us[^\n]*rc=0',c): inv.append('A-request-not-verified')
        if 'FSU: updated' in c: inv.append('A-unexpected-event')
    else:
        if not re.search(r'FSU: request \[52\.\.150\] us[^\n]*rc=0',c): inv.append('B-request-not-verified')
        if not re.search(r'FSU: updated status=0x00 spacing=52 us',c): inv.append('B-no-completion')
    if 'PHY tx=2 rx=2' not in c: inv.append('no-2M-confirm')
    for mk in ('FATAL','USAGE FAULT','Halting'):
        if mk in c or mk in p: inv.append(f'marker:{mk}')
    dvals=[int(m.group(1)) for m in re.finditer(r'disc=(\d+)\(',c)]
    if dvals and dvals[-1]!=0: inv.append(f'disc={dvals[-1]}')
    canc=[int(m.group(1)) for m in re.finditer(r'cancels=(\d+)\(',c)]
    if not canc or canc[-1]!=0: inv.append(f'cancels={canc[-1] if canc else None}')
    g=sorted(int(m.group(1)) for m in re.finditer(r'cgap_us\[min/avg/max\]=(\d+)/',c) if int(m.group(1))>1000)
    cg=g[len(g)//2] if g else None
    return med,cg,inv

def main():
    seq=[]
    for t in sorted(glob.glob(f'{EV}/m0-pay2m-f*-central.log')):
        tag=os.path.basename(t)[:-12]
        m=re.match(r'm0-pay2m-f(\d+)-t(\d+)',tag)
        seq.append((int(m.group(2)),int(m.group(1)),tag))
    seq.sort()
    cells=[];bad=False
    for ts,v,tag in seq:
        med,cg,inv=cell(tag,v)
        if inv: bad=True
        print(f'{tag}: arm={v} rx_med={med} cgap_min_med={cg} {"INVALID:"+";".join(inv) if inv else "valid"}')
        cells.append((v,med))
    d=[];o=[]
    i=0
    while i+1<len(cells):
        (v1,m1),(v2,m2)=cells[i],cells[i+1]
        if {v1,v2}=={150,52} and m1 and m2:
            a,b=(m1,m2) if v1==150 else (m2,m1)
            d.append((b-a)/a*100); o.append('AB' if v1==150 else 'BA')
            print(f'pair {len(d)} [{o[-1]}]: A {a} -> B {b}  {(b-a)/a*100:+.2f}%')
            i+=2
        else: i+=1
    if len(d)>=2:
        n=len(d);mean=sum(d)/n
        sd=math.sqrt(sum((x-mean)**2 for x in d)/(n-1))
        half=TCRIT[n-1]*sd/math.sqrt(n)
        print(f'mean paired delta {mean:+.2f}%  95% CI [{mean-half:+.2f}, {mean+half:+.2f}] (n={n})')
        ab=[x for x,y in zip(d,o) if y=='AB'];ba=[x for x,y in zip(d,o) if y=='BA']
        if ab and ba: print(f'order effect: AB {sum(ab)/len(ab):+.2f}% vs BA {sum(ba)/len(ba):+.2f}%')
    if bad: print('VERDICT: INVALID — cell validity failure')
    else: print('Interpretation per registered rules: any result = observed '
                'system payoff under masking; null says nothing about '
                'radio-level benefit.')

if __name__=='__main__':
    main()
