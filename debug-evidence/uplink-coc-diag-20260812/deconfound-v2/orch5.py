#!/usr/bin/env python3
"""Deconfound v3 — isolate teardown MODE from central-reboot-induced Order-A reconnect.

Finding: the uplink wedge is an ORDER-A setup phenomenon (peripheral advertising BEFORE the
central joins => wedge; central up first, peripheral joins => healthy). Resetting the central
to induce an abrupt 0x08 makes it REBOOT and rejoin AFTER the peripheral re-advertises = Order
A. So the earlier 'abrupt stalls / graceful recovers' may be central-reboot(Order-A) vs
same-boot(Order-B), NOT the teardown mode.

This 3-cell test (ONE central binary) holds session 1 healthy (Order B) and drained, then
tears down in the console-selected mode:
  a = abrupt+reboot   (0x08, central reboots -> session 2 is Order A)
  g = graceful+reboot (0x13, central reboots -> session 2 is Order A)   <-- reviewer's control
  s = graceful+sameboot (0x13, central stays up -> session 2 is Order B)
Predictions if ORDER/reboot is the cause: a WEDGE, g WEDGE, s RECOVER (teardown mode irrelevant).
If teardown MODE is the cause: a WEDGE, g RECOVER, s RECOVER.

Bring-up: a pre-establish parks the periph in a CONNECTED (non-advertising) state; then reset
the CENTRAL (periph holds the dead link for the supervision timeout, not advertising), inject
mode, and reset the PERIPH fresh at t=3 -> central (up first) connects Order-B healthy session 1.

Usage: orch5.py <mode:a|g|s> <periph_port> <central_port> <outdir>
"""
import sys, time, threading, subprocess, serial

CENTRAL_SN = "1057794857"
PERIPH_SN  = "1057719509"

def reset(sn):
    subprocess.run(["nrfutil","device","reset","--serial-number",sn], capture_output=True)

def central_io(port, secs, out, mode, t0):
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
    except Exception as e:
        open(out,'w').write(f'OPEN-FAIL {port}: {e}\n'); return
    with open(out,'w') as f:
        while time.monotonic()-t0 < secs:
            t = time.monotonic()-t0
            if t < 3.4:                                   # inject mode ONLY during the first boot (before periph reset)
                s.write(mode.encode()); s.flush()
            # After a reboot (modes a/g) the central RAM resets so g_mode defaults 'n'
            # (measure-only) -> session 2 is NOT torn down. No 'n' injection needed, and
            # injecting it risks overwriting the mode before the connect+16s teardown fires.
            try: ln = s.readline()
            except Exception as e: f.write(f'[readerr {e}]\n'); break
            if ln:
                f.write(f'{t:8.2f} ' + ln.decode('utf-8','replace')); f.flush()
    s.close()

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
                f.write(f'{time.monotonic()-t0:8.2f} ' + ln.decode('utf-8','replace')); f.flush()
    s.close()

if __name__ == '__main__':
    mode, pport, cport, outdir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    SECS = 52.0
    # Pre-establish: central up first, periph joins -> they connect (Order B). Periph ends
    # CONNECTED (not advertising). Central mode defaults 'n' (no teardown).
    print(f"[{mode}] pre-establish: reset CENTRAL then PERIPH, let them connect (~11s)")
    reset(CENTRAL_SN); time.sleep(2.5); reset(PERIPH_SN); time.sleep(11.0)
    # Main run: reset CENTRAL (up first; periph holds dead link, not advertising), inject
    # mode, reset PERIPH fresh at t=3 -> Order-B healthy session 1.
    print(f"[{mode}] main: reset CENTRAL (Order B); inject mode during boot")
    reset(CENTRAL_SN)
    t0 = time.monotonic()
    ts = [threading.Thread(target=central_io, args=(cport, SECS, f'{outdir}/v3-{mode}-central.log', mode, t0)),
          threading.Thread(target=reader,     args=(pport, SECS, f'{outdir}/v3-{mode}-periph.log', t0))]
    for t in ts: t.start()
    time.sleep(3.0)
    print(f"[{mode}] reset PERIPH -> fresh, Order-B prompt connect (session 1)")
    reset(PERIPH_SN)
    for t in ts: t.join()
    print(f"[{mode}] capture done")
