import os, re, subprocess, sys, time
REPO, S = sys.argv[1], sys.argv[2]
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
def sh(c, t=180): return subprocess.run(c, capture_output=True, text=True, timeout=t)
def flash(h, sn): assert sh(['nrfutil','device','program','--firmware',h,'--serial-number',sn]).returncode == 0
caps=f'{S}/caps'; os.makedirs(caps, exist_ok=True)
flash(f'{S}/b/rep-ini/zephyr/zephyr.hex', CEN)
for i, v in enumerate(['patched','unpatched','unpatched','patched']):
    tag=f'patch-{v}-r{i+1}'
    flash(f'{S}/b/{"rep-acc-patched" if v=="patched" else "rep-acc"}/zephyr/zephyr.hex', PER)
    p=subprocess.Popen([sys.executable, os.path.join(REPO,'tools','capture-tool.py'),'70',f'{CTTY}:{tag}-cen',f'{PTTY}:{tag}-per'],
                       env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    for sn in (PER, CEN): sh(['nrfutil','device','reset','--serial-number',sn], 60)
    p.wait(timeout=140)
    # per acceptor connection: healthy if done keeps up with sent, wedged if the WEDGED marker appears
    per=open(f'{caps}/{tag}-per.log',errors='ignore').read().splitlines()
    conns=[]; cur=None
    for l in per:
        if 'L2CAP channel connected' in l: cur={'wedged':False,'last':None}; conns.append(cur)
        elif cur is not None:
            m=re.search(r'sent=(\d+) done=(\d+)',l)
            if m: cur['last']=(int(m.group(1)),int(m.group(2)))
            if 'TX WEDGED' in l: cur['wedged']=True
            if 'L2CAP channel disconnected' in l: cur=None
    print(tag, 'connections', len(conns), 'stalled(done=0)', sum(1 for c in conns if c['last'] and c['last'][1]==0), [c['last'] for c in conns], flush=True)
