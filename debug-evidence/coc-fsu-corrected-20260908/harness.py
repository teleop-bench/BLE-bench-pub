#!/usr/bin/env python3
"""CORRECTED CoC/GATT one-way FSU campaign — FSU held to end-of-run.

Supersedes coc-fsu-interval-20260908 + the CoC cells of fsu-campaign-20260907, which were INVALID:
their sinks had BT_GAP_AUTO_UPDATE_CONN_PARAMS=y, so the ~5 s param-update silently reverted tIFS to
150 µs mid-measurement while a stale `fsu=52` token kept printing → measured post-revert throughput →
false "CoC FSU ≈ 0%". Here every responder is auto-update-OFF (FSU held); each measurement VERIFIES the
treatment held (steady late-window + no mid-run throughput step-down) and keeps its raw logs.

Cells (open controller, 2M, DLE-251):
  CoC  15 ms / 480 B : on12/off12 central + sink_noau (coc-sink, GAP_AUTO_UPDATE=n)
  GATT 7.5 ms        : c-gatt-open-{fsu,off}-7p5 + p-gatt-sink-open (z54-lat-periph, GAP_AUTO_UPDATE=n)
Note interval differs by transport (CoC 15 / GATT 7.5) — report per-transport FSU deltas, do NOT claim
like-for-like transport comparison (see reviewer P1-4).
"""
import os, re, sys, time, subprocess, statistics, math, hashlib, shutil
REPO='<REPO>'
D=REPO+'/debug-evidence/coc-fsu-corrected-20260908'
CEN,PER='1057794857','1057719509'
CTTY='/dev/cu.usbmodem0010577948573'; PTTY='/dev/cu.usbmodem0010577195093'
SECS=30; CAPS=D+'/caps'; FW=D+'/firmware'
# (transport, fsu, central_hex, sink_hex, label)
CELLS=[
 ('CoC','on', '/tmp/cocsweep/on12/zephyr/zephyr.hex','/tmp/sink_noau/zephyr/zephyr.hex','15ms/480B'),
 ('CoC','off','/tmp/cocsweep/off12/zephyr/zephyr.hex','/tmp/sink_noau/zephyr/zephyr.hex','15ms/480B'),
 ('GATT','on', REPO+'/prebuilt-hexes/c-gatt-open-fsu-7p5.hex', REPO+'/prebuilt-hexes/p-gatt-sink-open.hex','7.5ms'),
 ('GATT','off',REPO+'/prebuilt-hexes/c-gatt-open-off-7p5.hex', REPO+'/prebuilt-hexes/p-gatt-sink-open.hex','7.5ms'),
]
def prog(h,d): subprocess.run(['nrfutil','device','program','--firmware',h,'--serial-number',d],capture_output=True,timeout=120)
def rst(d): subprocess.run(['nrfutil','device','reset','--serial-number',d],capture_output=True,timeout=30)
def rateseries(tag):
    out=[]
    for suf in ('-per','-cen'):
        try:
            for l in open(f'{CAPS}/{tag}{suf}.log'):
                m=re.search(r'^\s*([0-9.]+)\s+SINK rx:\s*([0-9]+) KB/s',l) or re.search(r'^\s*([0-9.]+)\s+P t=\d+s rxkBps=([0-9]+)',l)
                if m: out.append((float(m.group(1)),int(m.group(2))))
            if out: return out
        except Exception: pass
    return out
def meas(cell,rep):
    tp,fsu,cen,sink,lab=cell; tag=f'{tp}_{fsu}_r{rep}'
    prog(sink,PER); prog(cen,CEN)
    os.makedirs(CAPS,exist_ok=True)
    p=subprocess.Popen(['python3',REPO+'/tools/capture-tool.py',str(SECS),f'{CTTY}:{tag}-cen',f'{PTTY}:{tag}-per'],
                       env=dict(os.environ,CAP_OUTDIR=CAPS),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS+40)
    except Exception: p.kill()
    s=rateseries(tag); vals=[k for _,k in s if k>0]
    if not s: return (0,0,0,'-',False)
    tmax=s[-1][0]
    early=[k for t,k in s if 3<=t<=6 and k>0]
    late =[k for t,k in s if t>=max(tmax-8,15) and k>0]
    e=statistics.mean(early) if early else 0; l=statistics.mean(late) if late else 0
    drop=(e-l)/e*100 if e else 0
    sp='-'
    try:
        t=open(f'{CAPS}/{tag}-cen.log').read()
        m=re.findall(r'spacing=(\d+)',t); sp=m[-1] if m else '-'
    except Exception: pass
    held = (fsu=='off') or (drop<=6 and sp=='52')   # FSU arm must not STEP DOWN (positive drop) + still 52; ramp-up (neg drop) ok
    return (l,e,drop,sp,held)
def main():
    reps=int(sys.argv[1]) if len(sys.argv)>1 else 15
    os.makedirs(FW,exist_ok=True); os.makedirs(CAPS,exist_ok=True)
    # persist firmware + hashes (fix evidence-package critique)
    seen=set()
    for _,_,cen,sink,_ in CELLS:
        for h in (cen,sink):
            if h in seen or not os.path.exists(h): continue
            seen.add(h); shutil.copy(h, FW+'/'+os.path.basename(os.path.dirname(os.path.dirname(h)))+'_'+os.path.basename(h))
    with open(FW+'/SHA256SUMS','w') as f:
        for fn in sorted(os.listdir(FW)):
            if fn.endswith('.hex'):
                f.write(hashlib.sha256(open(FW+'/'+fn,'rb').read()).hexdigest()+'  '+fn+'\n')
    data={(t,f):[] for t,f,_,_,_ in CELLS}; held={(t,f):[] for t,f,_,_,_ in CELLS}
    RES=D+'/results.jsonl'
    for rep in range(1,reps+1):
        order=CELLS if rep%2 else list(reversed(CELLS))
        for cell in order:
            tp,fsu,_,_,lab=cell
            l,e,drop,sp,h=meas(cell,rep)
            data[(tp,fsu)].append(l); held[(tp,fsu)].append(h)
            with open(RES,'a') as f: f.write(f'{{"round":{rep},"tp":"{tp}","fsu":"{fsu}","label":"{lab}","steady":{l:.1f},"early":{e:.1f},"drop":{drop:.1f},"spacing":"{sp}","held":{str(h).lower()}}}\n')
            print(f'  r{rep} {tp:4} {fsu:3}: steady {l:6.1f}  early {e:6.1f}  drop {drop:+4.0f}%  sp={sp:>3}  held={h}',flush=True)
        # live summary
        L=[f'CORRECTED CoC/GATT FSU (held) — {rep} rounds, {SECS}s',''];
        for tp in ('CoC','GATT'):
            on=[x for x in data[(tp,'on')] if x>0]; off=[x for x in data[(tp,'off')] if x>0]
            deltas=[]
            for rr in range(rep):
                if rr<len(data[(tp,'on')]) and rr<len(data[(tp,'off')]) and data[(tp,'off')][rr]>0:
                    deltas.append(100*(data[(tp,'on')][rr]/data[(tp,'off')][rr]-1))
            hr=100*sum(held[(tp,'on')])//max(len(held[(tp,'on')]),1)
            if len(deltas)>=2:
                d=statistics.mean(deltas); ci=1.96*statistics.stdev(deltas)/math.sqrt(len(deltas))
                L.append(f'  {tp:4}: off {statistics.mean(off) if off else 0:6.1f}  on {statistics.mean(on) if on else 0:6.1f}  FSU {d:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)}) held-on={hr}%')
        open(D+'/summary.txt','w').write('\n'.join(L))
    print('DONE',reps,'rounds')
if __name__=='__main__': main()
