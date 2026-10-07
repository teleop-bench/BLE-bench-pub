#!/usr/bin/env python3
"""Endpoint-only connection HEALTH harness (no observer, no build). Deterministic
command-gated startup (fixes the reset race): reset both -> wait Q2READY from
each -> command peripheral 'A' (advertise) -> wait ADV-READY -> command central
'C' (scan/connect) -> wait connected + Q2CONN -> preflight counters.

PASS requires: exactly one Q2CONN + central connected; central & periph AA match;
central sched advancing; central tx advancing; periph sched advancing; periph
tx (CRC-good response opportunities) advancing; no disconnect; ONE boot per role;
distinct DEVICEIDs; single session; correct Q2CONN map. A stale buffered Q2READY
causes CONSERVATIVE rejection (n_ready>1), not silent ignore.

Usage: q2_health.py --central-port P --periph-port P --central-devid S
       --periph-devid S --outdir D [--secs 5]
"""
import argparse, os, re, sys, time, threading
import serial

def main():
    ap = argparse.ArgumentParser()
    for x in ('central-port','periph-port','central-devid','periph-devid','outdir'):
        ap.add_argument('--'+x, required=True)
    ap.add_argument('--secs', type=int, default=5)
    ap.add_argument('--expect-map', default='000c000000')
    ap.add_argument('--expect-cdev', default='')   # optional FICR DEVICEID identity
    ap.add_argument('--expect-pdev', default='')   # (else only distinctness is enforced)
    a = {k.replace('-','_'):v for k,v in vars(ap.parse_args()).items()}
    os.makedirs(a['outdir'], exist_ok=True)
    import subprocess
    def reset(d): subprocess.run(['nrfutil','device','reset','--serial-number',d],stdout=-3,stderr=-3)
    C = serial.Serial(a['central_port'],115200,timeout=0.1)
    P = serial.Serial(a['periph_port'],115200,timeout=0.1)
    clog, plog = [], []
    stop = threading.Event()
    def rd(s,l):
        while not stop.is_set():
            ln=s.readline()
            if ln: l.append(ln.decode('utf-8','replace').rstrip())
    C.reset_input_buffer(); P.reset_input_buffer()
    tc=threading.Thread(target=rd,args=(C,clog),daemon=True); tp=threading.Thread(target=rd,args=(P,plog),daemon=True)
    tc.start(); tp.start()
    def wait(log, tok, to):
        t=time.time()
        while time.time()-t<to:
            if any(tok in x for x in log): return True
            time.sleep(0.05)
        return False
    # deterministic startup (readers already draining; reset AFTER flush so the
    # post-boot Q2READY is captured, not cleared)
    reset(a['periph_devid']); reset(a['central_devid'])
    ok_ready = wait(clog,'Q2READY role=C',15) and wait(plog,'Q2READY role=P',15)
    P.write(b'A'); adv = wait(plog,'ADV-READY',10)
    C.write(b'C'); conn = wait(clog,'CENTRAL connected',15) and wait(clog,'Q2CONN',5)
    time.sleep(a['secs'])
    stop.set(); time.sleep(0.3)
    open(os.path.join(a['outdir'],'central.txt'),'w').write('\n'.join(clog))
    open(os.path.join(a['outdir'],'periph.txt'),'w').write('\n'.join(plog))
    C.close(); P.close()
    # ignore ALL rows before the LAST Q2READY (the fresh post-reset boot); also
    # catches a mid-run reboot (>1 Q2READY after the window start).
    def fresh(log, role):
        idx = [i for i,x in enumerate(log) if f'Q2READY role={role}' in x]
        return (log[idx[-1]:] if idx else []), len(idx)
    clog, n_cready = fresh(clog, 'C'); plog, n_pready = fresh(plog, 'P')
    def rd_ready(log, role):
        for x in log:
            m = re.search(rf'Q2READY role={role} dev=([0-9a-f]+) boottag=0x([0-9a-f]+)', x)
            if m: return m.group(1), m.group(2)
        return None, None
    cdev, ctag = rd_ready(clog,'C'); pdev, ptag = rd_ready(plog,'P')
    def evts(log,role):
        out=[]
        for x in log:
            m=re.search(rf'Q2EVT role={role} aa=0x([0-9a-f]+) sess=(\d+).*sched=(\d+) tx=(\d+)',x)
            if m: out.append((int(m.group(1),16),int(m.group(2)),int(m.group(3)),int(m.group(4))))
        return out
    ce, pe = evts(clog,'C'), evts(plog,'P')
    conn_m = re.search(r'Q2CONN .*map=([0-9a-f]+)', '\n'.join(clog))
    got_map = conn_m.group(1) if conn_m else None
    nconn = sum('Q2CONN' in x for x in clog); ndis = sum('disconnected' in x for x in clog+plog)
    def adv_chk(e): return len(e)>=2 and e[-1][2]>e[0][2]   # sched advanced
    def tx_chk(e):  return len(e)>=2 and e[-1][3]>e[0][3]    # tx advanced
    def sess_ok(e):   # exactly one NONZERO (connected) session -> no reconnect
        nz={r[1] for r in e if r[1]>0}; return len(nz)==1
    caa = ce[-1][0] if ce else 0; paa = pe[-1][0] if pe else 0
    checks = [
      ('READY from both', ok_ready and cdev and pdev), ('periph ADV-READY', adv),
      ('exactly one boot per role (no mid-run reboot)', n_cready==1 and n_pready==1),
      ('distinct DEVICEIDs', bool(cdev and pdev and cdev!=pdev)),
      ('DEVICEIDs match expected (or none given)',
       (not a['expect_cdev'] or cdev==a['expect_cdev']) and
       (not a['expect_pdev'] or pdev==a['expect_pdev'])),
      ('central connected + Q2CONN', conn), ('exactly one Q2CONN', nconn==1),
      (f'Q2CONN map=={a["expect_map"]}', got_map==a['expect_map']),
      ('no disconnect', ndis==0), ('central AA==periph AA (nonzero)', caa==paa!=0),
      ('single session both (no reconnect)', sess_ok(ce) and sess_ok(pe)),
      ('central sched advancing', adv_chk(ce)), ('central tx advancing', tx_chk(ce)),
      ('periph sched advancing', adv_chk(pe)), ('periph tx advancing', tx_chk(pe)),
    ]
    lines = [f'central: dev={cdev} boottag={ctag} lastEVT={ce[-1] if ce else None}',
             f'periph:  dev={pdev} boottag={ptag} lastEVT={pe[-1] if pe else None}',
             f'Q2CONN map={got_map}']
    allok=True
    for n,v in checks:
        v=bool(v); lines.append(f'  [{"PASS" if v else "FAIL"}] {n}'); allok = allok and v
    lines.append('=== HEALTH: ' + ('PASS' if allok else 'FAIL') + ' ===')
    out = '\n'.join(lines); print(out)
    open(os.path.join(a['outdir'],'verdict.txt'),'w').write(out+'\n')
    sys.exit(0 if allok else 3)

if __name__=='__main__': main()
