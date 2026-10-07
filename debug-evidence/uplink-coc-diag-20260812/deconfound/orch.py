#!/usr/bin/env python3
"""Deconfound orchestrator: Order-B bring-up -> healthy session 1 -> pause drains it
-> induce teardown WHILE DRAINED -> observe session 2. Holds sender condition (drained)
constant; the teardown MODE is the only variable between the two cells.

Usage: orch.py <mode:abrupt|graceful> <periph_port> <central_port> <outdir>
  abrupt   : reset the central mid-drain (no LL_TERMINATE) -> periph sees 0x08 supervision timeout
  graceful : (central runs the gdisc firmware; it issues LL_TERMINATE at connect+10s on its own)

Central SN 1057794857, Periph SN 1057719509. Reset via nrfutil.
"""
import sys, time, threading, subprocess, serial

CENTRAL_SN = "1057794857"
PERIPH_SN  = "1057719509"

def reset(sn):
    subprocess.run(["nrfutil","device","reset","--serial-number",sn],
                   capture_output=True)

def reader(port, secs, out, t0):
    try:
        s = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        open(out,'w').write(f'OPEN-FAIL {port}: {e}\n'); return
    with open(out,'w') as f:
        while time.monotonic()-t0 < secs:
            try: ln = s.readline()
            except Exception as e: f.write(f'[readerr {e}]\n'); break
            if ln:
                f.write(f'{time.monotonic()-t0:8.2f} ' + ln.decode('utf-8','replace'))
                f.flush()
    s.close()

if __name__ == '__main__':
    mode, pport, cport, outdir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    SECS = 48.0
    # Order B: central up first, then periph advertises -> healthy session 1
    print(f"[{mode}] reset CENTRAL (Order B start)"); reset(CENTRAL_SN)
    t0 = time.monotonic()
    ts = [threading.Thread(target=reader, args=(pport, SECS, f'{outdir}/{mode}-periph.log', t0)),
          threading.Thread(target=reader, args=(cport, SECS, f'{outdir}/{mode}-central.log', t0))]
    for t in ts: t.start()
    time.sleep(3.0)
    print(f"[{mode}] reset PERIPH -> advertises, central connects (session 1)"); reset(PERIPH_SN)
    # session 1 streams; periph pause fires at connect+8s (~t=12) -> drains to out=0
    if mode.startswith('abrupt'):
        # wait well past the pause so the sender is DRAINED, then abruptly drop by resetting central
        while time.monotonic()-t0 < 20.0: time.sleep(0.2)
        print(f"[{mode}] reset CENTRAL mid-drain -> abrupt 0x08 (sender drained)"); reset(CENTRAL_SN)
    else:
        # graceful firmware issues LL_TERMINATE at connect+10s on its own; nothing to do
        print(f"[{mode}] graceful firmware self-terminates at connect+10s; observing")
    for t in ts: t.join()
    print(f"[{mode}] capture done")
