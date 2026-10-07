#!/usr/bin/env python3
"""Held-verified SDC (SoftDevice Controller) one-way FSU campaign — apples-to-apples with the open run.

Matches the open corrected run (coc-fsu-corrected-20260908): auto-update-OFF sinks (FSU held),
matched intervals (CoC 15ms/480B, GATT 7.5ms), counterbalanced, raw logs kept, firmware+hashes
persisted. SDC does NOT emit the app `spacing=52` token, so held-verification = the throughput does
not STEP DOWN mid-run (a revert would show as a step-down, exactly as the open CoC sink did).
Firmware: NCS v3.4.0 sysbuild, /tmp/sdc/*/<app>/zephyr/zephyr.hex.
"""
import os, re, sys, time, subprocess, statistics, math, hashlib, shutil
REPO='<REPO>'
D=REPO+'/debug-evidence/sdc-fsu-corrected-20260908'
CEN,PER='1057794857','1057719509'
CTTY='/dev/cu.usbmodem0010577948573'; PTTY='/dev/cu.usbmodem0010577195093'
SECS=30; CAPS=D+'/caps'; FW=D+'/firmware'; S='/tmp/sdc'
CELLS=[
 ('CoC','on', f'{S}/coc_on/coc-central/zephyr/zephyr.hex',   f'{S}/coc_sink/coc-sink/zephyr/zephyr.hex','15ms/480B'),
 ('CoC','off',f'{S}/coc_off/coc-central/zephyr/zephyr.hex',  f'{S}/coc_sink/coc-sink/zephyr/zephyr.hex','15ms/480B'),
 ('GATT','on', f'{S}/gatt_on/z54-lat-central/zephyr/zephyr.hex', f'{S}/gatt_sink/z54-lat-periph/zephyr/zephyr.hex','7.5ms'),
 ('GATT','off',f'{S}/gatt_off/z54-lat-central/zephyr/zephyr.hex',f'{S}/gatt_sink/z54-lat-periph/zephyr/zephyr.hex','7.5ms'),
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
    prog(sink,PER); prog(cen,CEN); os.makedirs(CAPS,exist_ok=True)
    p=subprocess.Popen(['python3',REPO+'/tools/capture-tool.py',str(SECS),f'{CTTY}:{tag}-cen',f'{PTTY}:{tag}-per'],
                       env=dict(os.environ,CAP_OUTDIR=CAPS),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS+40)
    except Exception: p.kill()
    s=rateseries(tag)
    if not s: return (0,0,0,False)
    tmax=s[-1][0]
    early=[k for t,k in s if 3<=t<=6 and k>0]; late=[k for t,k in s if t>=max(tmax-8,15) and k>0]
    e=statistics.mean(early) if early else 0; l=statistics.mean(late) if late else 0
    drop=(e-l)/e*100 if e else 0
    held=(fsu=='off') or (drop<=6)   # SDC: no token; held = no mid-run STEP-DOWN (revert would step down)
    return (l,e,drop,held)
def main():
    reps=int(sys.argv[1]) if len(sys.argv)>1 else 12
    os.makedirs(FW,exist_ok=True)
    seen=set()
    for _,_,cen,sink,_ in CELLS:
        for h in (cen,sink):
            if h in seen or not os.path.exists(h): continue
            seen.add(h); shutil.copy(h, FW+'/'+h.split('/tmp/sdc/')[1].replace('/','_'))
    with open(FW+'/SHA256SUMS','w') as f:
        for fn in sorted(os.listdir(FW)):
            if fn.endswith('.hex'): f.write(hashlib.sha256(open(FW+'/'+fn,'rb').read()).hexdigest()+'  '+fn+'\n')
    data={(t,f):[] for t,f,_,_,_ in CELLS}; held={(t,f):[] for t,f,_,_,_ in CELLS}
    RES=D+'/results.jsonl'
    for rep in range(1,reps+1):
        for cell in (CELLS if rep%2 else list(reversed(CELLS))):
            tp,fsu,_,_,lab=cell; l,e,drop,h=meas(cell,rep)
            data[(tp,fsu)].append(l); held[(tp,fsu)].append(h)
            with open(RES,'a') as f: f.write(f'{{"round":{rep},"tp":"{tp}","fsu":"{fsu}","label":"{lab}","steady":{l:.1f},"early":{e:.1f},"drop":{drop:.1f},"held":{str(h).lower()}}}\n')
            print(f'  r{rep} {tp:4} {fsu:3}: steady {l:6.1f} early {e:6.1f} drop {drop:+4.0f}% held={h}',flush=True)
        L=[f'SDC CoC/GATT FSU (held) — {rep} rounds, {SECS}s','']
        for tp in ('CoC','GATT'):
            on=[x for x in data[(tp,'on')] if x>0]; off=[x for x in data[(tp,'off')] if x>0]
            deltas=[100*(data[(tp,'on')][i]/data[(tp,'off')][i]-1) for i in range(min(len(data[(tp,'on')]),len(data[(tp,'off')]))) if data[(tp,'off')][i]>0]
            hr=100*sum(held[(tp,'on')])//max(len(held[(tp,'on')]),1)
            if len(deltas)>=2:
                d=statistics.mean(deltas); ci=1.96*statistics.stdev(deltas)/math.sqrt(len(deltas))
                L.append(f'  {tp:4}: off {statistics.mean(off) if off else 0:6.1f}  on {statistics.mean(on) if on else 0:6.1f}  FSU {d:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)}) held-on={hr}%')
        open(D+'/summary.txt','w').write('\n'.join(L))
    print('DONE')
if __name__=='__main__': main()
