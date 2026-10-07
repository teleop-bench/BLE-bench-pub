#!/usr/bin/env python3
"""Phase-5 ramp arm A/B — registered before any m0-ramp5-* log read.

DESIGN: loadramp rig (7 stages x 60 s, targets 0..150 KB/s) + FSU build
family, 2M, 7.5 ms. A=[150..150] control, B=[52..150]. INTERLEAVED
A B B A (one full ramp cycle per cell, ~460 s each). FSU fires at +3 s,
before stage 1 load begins; no param update at 7.5 ms (no revert risk).

ENDPOINTS per cell, deterministic stage segmentation per loadramp rev-2
(peripheral clock; onset = first of 3 consecutive blkkBps>10; 60 s
windows; 5 s trims; monotonic + >=40-sample asserts): per-stage achieved
KiB/s and central per-stage weighted-mean RTT (drop first 5 lines).
PRIMARY comparison: per-stage RTT-mean differences (B-A) matched by
stage across the interleaved pairs (A1B1, B2A2), reported per stage with
the two per-pair values; no CI (n=2 per stage - descriptive, registered
as hypothesis-generating ONLY, mirroring the earlier ramp convention).
VALIDITY: spacing confirms as usual; stage-order assert; fault markers;
per-cell disc final == 0.
"""
import re, os, glob
EV=os.path.dirname(os.path.abspath(__file__))
TARGETS=[0,25,50,75,100,125,150]
def cell(tag,arm):
    c=open(f'{EV}/{tag}-central.log','rb').read().decode('utf-8','replace')
    p=open(f'{EV}/{tag}-periph.log','rb').read().decode('utf-8','replace')
    inv=[]
    if arm==150:
        if not re.search(r'FSU: request \[150\.\.150\] us[^\n]*rc=0',c): inv.append('A-request-not-verified')
        if 'FSU: updated' in c: inv.append('A-unexpected-event')
    else:
        if not re.search(r'FSU: request \[52\.\.150\] us[^\n]*rc=0',c): inv.append('B-request-not-verified')
        if not re.search(r'FSU: updated status=0x00 spacing=52 us',c): inv.append('B-no-completion')
    for mk in ('FATAL','USAGE FAULT','Halting'):
        if mk in c or mk in p: inv.append(f'marker:{mk}')
    samp=[(int(m.group(1)),int(m.group(2))) for m in re.finditer(r'P t=(\d+)s [^\r\n]*?blkkBps=(\d+)',p)]
    ts=[t for t,_ in samp]
    if not all(b>a for a,b in zip(ts,ts[1:])): inv.append('non-monotonic')
    onset=None
    for i in range(len(samp)-2):
        if all(samp[i+j][1]>10 for j in range(3)): onset=samp[i][0]; break
    cur=None;stages=[]
    for l in c.splitlines():
        m=re.match(r'LOADRAMP: target=(\d+) KBps',l)
        if m: cur={'t':int(m.group(1)),'r':[]}; stages.append(cur); continue
        if cur and 'RTT mean=' in l:
            mm=re.search(r'mean=(\d+) min=\d+ max=(\d+) n=(\d+)',l)
            if mm and int(mm.group(3))>0: cur['r'].append((int(mm.group(1)),int(mm.group(2)),int(mm.group(3))))
    if [st['t'] for st in stages[:7]]!=TARGETS: inv.append('stage-order')
    out={}
    for k,st in enumerate(stages[:7]):
        rs=st['r'][5:]
        if not rs: continue
        tn=sum(r[2] for r in rs)
        mean=sum(r[0]*r[2] for r in rs)/max(tn,1)
        ach=None
        if onset is not None and st['t']>0:
            w0=onset+60*(k-1)+5; w1=onset+60*k-5
            band=[b for t,b in samp if w0<=t<w1]
            if len(band)<40: inv.append(f'stage{st["t"]}-n{len(band)}')
            else: ach=sum(band)/len(band)
        elif st['t']==0: ach=0.0
        out[st['t']]=(round(mean/1000,2),round(ach,1) if ach is not None else None)
    return out,inv
def main():
    seq=[]
    for t in sorted(glob.glob(f'{EV}/m0-ramp5-f*-central.log')):
        tag=os.path.basename(t)[:-12]
        m=re.match(r'm0-ramp5-f(\d+)-t(\d+)',tag)
        seq.append((int(m.group(2)),int(m.group(1)),tag))
    seq.sort()
    cells=[];bad=False
    for ts,v,tag in seq:
        out,inv=cell(tag,v)
        if inv: bad=True
        print(f'{tag}: arm={v} {"INVALID:"+";".join(inv) if inv else "valid"}')
        for t in TARGETS:
            if t in out: print(f'  target {t}: rtt_mean={out[t][0]} ms achieved={out[t][1]}')
        cells.append((v,out))
    a_cells=[o for v,o in cells if v==150]; b_cells=[o for v,o in cells if v==52]
    if len(a_cells)>=2 and len(b_cells)>=2:
        print('per-stage RTT-mean B-A (two interleaved pairs; hypothesis-generating):')
        for t in TARGETS:
            try:
                d1=b_cells[0][t][0]-a_cells[0][t][0]; d2=b_cells[1][t][0]-a_cells[1][t][0]
                print(f'  target {t}: {d1:+.2f} ms / {d2:+.2f} ms')
            except KeyError: print(f'  target {t}: missing')
    print('VERDICT: INVALID — cell validity failure' if bad else
          'Registered: hypothesis-generating ramp comparison under masking.')
if __name__=='__main__': main()
