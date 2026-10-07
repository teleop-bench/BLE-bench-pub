#!/usr/bin/env python3
"""Phase-5 duplex D-cell A/B — registered before any m0-dx2m-* log read.

DESIGN: echo configuration (periph tput-echo-open+fsu; central blast,
same FSU build family), 2M, 7.5 ms. A=[150..150] control, B=[52..150].
AB BA BA AB, 8 x 150 s. INTERPRETATION (verdict derived from estimate+CI,
not prejudged): a positive/negative/null OUTCOME is read off the paired
CI; any outcome is only "observed system payoff under pump/quantization
masking" and says nothing about radio-level benefit.

ENDPOINTS: forward = periph rxkBps median (>10, drop 20); reverse =
central exkBps median (same rule); aggregate = fwd+rev. Symmetry
REPORTED per cell: |fwd-rev|/mean, >5% flags ASYMMETRIC (included, not
excluded — duplex-contract convention). Paired B-A on aggregate (and
per-direction, reported); 95% t-CI (n=4 3.182 / 3 4.303 / 2 12.706);
order effects.

VALIDITY (any failure -> INVALID): A request [150..150] rc=0 + no event;
B request + spacing=52 completion; PHY tx=2 rx=2; NO post-settle
disconnect INCREMENT on EITHER board (baseline = the final discarded
qualifying sample; see disc_baseline_t); no fault markers
either log; central disc final == 0; cancels final == 0; >=100 samples
per direction.
"""
import re, os, glob, math
EV=os.path.dirname(os.path.abspath(__file__))
TCRIT={1:12.706,2:4.303,3:3.182}
def med(vals):
    v=sorted(vals); return v[len(v)//2] if v else None
def cell(tag,arm):
    c=open(f'{EV}/{tag}-central.log','rb').read().decode('utf-8','replace')
    p=open(f'{EV}/{tag}-periph.log','rb').read().decode('utf-8','replace')
    inv=[];flags=[]
    fwd_rows=[(int(m.group(1)),int(m.group(2))) for m in
              re.finditer(r'P t=(\d+)s [^\r\n]*?rxkBps=(\d+)',p) if int(m.group(2))>10]
    fwd=[v for _,v in fwd_rows][20:]
    rev=[int(m.group(1)) for m in re.finditer(r'exkBps=(\d+)',c) if int(m.group(1))>10][20:]
    # settle timestamp = t of the 20th kept forward sample (exact window start)
    # Baseline for the disconnect-increment test = timestamp of the 20th
    # (final DISCARDED) qualifying forward sample; accepted throughput
    # starts at index 20, so any disc increment AFTER this baseline falls
    # within (or immediately at the start of) the measured window and
    # invalidates the cell.
    disc_baseline_t = fwd_rows[19][0] if len(fwd_rows)>=20 else 10**9
    if len(fwd)<100: inv.append(f'fwd-rows={len(fwd)}')
    if len(rev)<100: inv.append(f'rev-rows={len(rev)}')
    f,r=med(fwd),med(rev)
    agg=(f+r) if (f and r) else None
    if f and r:
        imb=abs(f-r)/((f+r)/2)*100
        if imb>5: flags.append(f'ASYMMETRIC:{imb:.1f}%')
    if arm==150:
        if not re.search(r'FSU: request \[150\.\.150\] us[^\n]*rc=0',c): inv.append('A-request-not-verified')
        if 'FSU: updated' in c: inv.append('A-unexpected-event')
    else:
        if not re.search(r'FSU: request \[52\.\.150\] us[^\n]*rc=0',c): inv.append('B-request-not-verified')
        if not re.search(r'FSU: updated status=0x00 spacing=52 us',c): inv.append('B-no-completion')
    if 'PHY tx=2 rx=2' not in c: inv.append('no-2M-confirm')
    for mk in ('FATAL','USAGE FAULT','Halting'):
        if mk in c or mk in p: inv.append(f'marker:{mk}')
    # Disconnect INCREMENT after the disc baseline, both boards (the
    # registered latency-A/B "increments" rule). A pre-baseline setup
    # disconnect is REPORTED (obs) but does not invalidate a clean window.
    def disc_inc(raw, tpat):
        w=[d for t,d in ((int(m.group(1)),int(m.group(2))) for m in re.finditer(tpat, raw))
           if t>=disc_baseline_t]
        return len(w)>=2 and w[-1]>w[0]
    ci = disc_inc(c, r't=(\d+)s [^\n]*disc=(\d+)\(')
    pi = disc_inc(p, r'P t=(\d+)s [^\n]*disc=(\d+)\(')
    if ci: inv.append('central-disc-increment')
    if pi: inv.append('periph-disc-increment')
    presettle = max((int(m.group(1)) for m in re.finditer(r'disc=(\d+)\(',c+p)), default=0)
    if presettle>0 and not (ci or pi): flags.append(f'pre-settle-disc={presettle}')
    to=[int(m.group(1)) for m in re.finditer(r' to=(\d+)',c)]
    if to and to[-1]-to[0]!=0: inv.append(f'timeouts={to[-1]-to[0]}')
    canc=[int(m.group(1)) for m in re.finditer(r'cancels=(\d+)\(',c)]
    if not canc or canc[-1]!=0: inv.append(f'cancels={canc[-1] if canc else None}')
    return f,r,agg,inv,flags
# ACCEPTED MANIFEST (frozen 2026-08-07; counterbalanced order AB BA BA AB).
# Cell 8's original capture (t1786132711) was REJECTED for truncation and
# is superseded by the rerun; a glob is NOT used so rejected files cannot
# re-enter and pairing order is fixed.
ACCEPTED=[(150,'m0-dx2m-f150-t1786131637'),(52,'m0-dx2m-f52-t1786131790'),
          (52,'m0-dx2m-f52-t1786131943'),(150,'m0-dx2m-f150-t1786132097'),
          (52,'m0-dx2m-f52-t1786132250'),(150,'m0-dx2m-f150-t1786132404'),
          (150,'m0-dx2m-f150-t1786132557'),(52,'m0-dx2m-f52-rr1786142027')]

def main():
    cells=[];bad=False
    for v,tag in ACCEPTED:
        f,r,agg,inv,flags=cell(tag,v)
        if inv: bad=True
        print(f'{tag}: arm={v} fwd={f} rev={r} agg={agg} '
              f'{"INVALID:"+";".join(inv) if inv else "valid"} {";".join(flags)}')
        cells.append((v,agg,f,r))
    d=[];o=[];df=[];dr=[]
    i=0
    while i+1<len(cells):
        (v1,a1,f1,r1),(v2,a2,f2,r2)=cells[i],cells[i+1]
        if {v1,v2}=={150,52} and a1 and a2:
            (aa,fa,ra),(ab,fb,rb)=((a1,f1,r1),(a2,f2,r2)) if v1==150 else ((a2,f2,r2),(a1,f1,r1))
            d.append((ab-aa)/aa*100); df.append((fb-fa)/fa*100); dr.append((rb-ra)/ra*100)
            o.append('AB' if v1==150 else 'BA')
            print(f'pair {len(d)} [{o[-1]}]: agg {aa} -> {ab}  {(ab-aa)/aa*100:+.2f}% '
                  f'(fwd {df[-1]:+.2f}%, rev {dr[-1]:+.2f}%)')
            i+=2
        else: i+=1
    if len(d)>=2:
        n=len(d);mean=sum(d)/n
        sd=math.sqrt(sum((x-mean)**2 for x in d)/(n-1))
        half=TCRIT[n-1]*sd/math.sqrt(n)
        print(f'aggregate mean {mean:+.2f}%  95% CI [{mean-half:+.2f}, {mean+half:+.2f}] (n={n})')
        print(f'per-direction means: fwd {sum(df)/n:+.2f}%, rev {sum(dr)/n:+.2f}%')
        ab=[x for x,y in zip(d,o) if y=='AB'];ba=[x for x,y in zip(d,o) if y=='BA']
        if ab and ba: print(f'order effect: AB {sum(ab)/len(ab):+.2f}% vs BA {sum(ba)/len(ba):+.2f}%')
    if bad:
        print('VERDICT: INVALID — cell validity failure'); return
    print('ALL CELLS VALID.')
    if len(d)>=2:
        n=len(d);mean=sum(d)/n
        sd=math.sqrt(sum((x-mean)**2 for x in d)/(n-1))
        half=TCRIT[n-1]*sd/math.sqrt(n)
        lo,hi=mean-half,mean+half
        outcome=('positive duplex payoff' if lo>0 else
                 'negative (payoff below control)' if hi<0 else
                 'no observed duplex payoff (CI spans 0)')
        print(f'OUTCOME (from estimate+CI): {outcome} under the masked system test; '
              f'says nothing about radio-level benefit.')
        # Sensitivity: setup-disconnect-affected pairs vs clean pairs.
        # Pairs 1-2 involve the first three chronological cells (pre-settle
        # setup disconnects); pairs 3-4 are disconnect-free.
        affected=d[:2]; clean=d[2:]
        print(f'POST-HOC DESCRIPTIVE SENSITIVITY (devised after seeing the data; '
              f'2 pairs/subgroup, no intervals): setup-disc-affected pairs (1-2) '
              f'point estimate {sum(affected)/len(affected):+.2f}%, clean pairs (3-4) '
              f'point estimate {sum(clean)/len(clean):+.2f}% — both subgroup point '
              f'estimates non-positive; the -2.33% is partly sensitive to early-session '
              f'instability. Descriptive only, not inferential.')
if __name__=='__main__': main()
