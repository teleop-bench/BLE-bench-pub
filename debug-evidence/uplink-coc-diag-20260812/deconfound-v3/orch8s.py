#!/usr/bin/env python3
"""Deconfound v3b — reboot WITHOUT Order-A, with a healthy session 1.

Uses the orch5 reset structure that reliably yields a healthy session 1 (central reset FIRST
so it is up/scanning; periph reset ~3 s later so it joins fresh = central-first / Order B),
BUT with the command-gated periph so the POST-REBOOT session 2 is also forced central-first
(Order B) instead of the Order-A that a normal reboot produces.

Timeline: reset CENTRAL (mode g) -> SCAN-READY#1 (ignored: periph not up yet). reset PERIPH at
+3 s. Host sends session-1 'V' at +4 s (periph up, central scanning) -> Order-B healthy session 1.
Drains; mode-g teardown (LL_TERMINATE 0x13) -> central REBOOTS -> SCAN-READY#2 -> host sends 'V'
-> Order-B session 2 (reboot WITHOUT Order A). Session-2 RECOVER => Order-A timing was the
trigger; STALL => fresh-central state independent of ordering.

Usage: orch8.py <periph_port> <central_port> <outdir> <tag>
"""
import sys, time, threading, subprocess, serial, queue

CENTRAL_SN = "1057794857"
PERIPH_SN  = "1057719509"
S1_V_AT = 4.0   # send session-1 'V' at t0+4s (after the +3s periph reset -> periph up, central scanning)

def reset(sn):
    subprocess.run(["nrfutil","device","reset","--serial-number",sn], capture_output=True)

def central_io(port, secs, out, t0, vq):
    try: s = serial.Serial(port, 115200, timeout=0.1)
    except Exception as e: open(out,'w').write(f'OPEN-FAIL {port}: {e}\n'); return
    with open(out,'w') as f:
        while time.monotonic()-t0 < secs:
            t = time.monotonic()-t0
            if t < 3.4:
                s.write(b'g'); s.flush()
            try: ln = s.readline()
            except Exception as e: f.write(f'[readerr {e}]\n'); break
            if ln:
                txt = ln.decode('utf-8','replace'); f.write(f'{t:8.2f} ' + txt); f.flush()
                if 'SCAN-READY' in txt: vq.put(t)
    s.close()

def periph_io(port, secs, out, t0, vq):
    try: s = serial.Serial(port, 115200, timeout=0.1)
    except Exception as e: open(out,'w').write(f'OPEN-FAIL {port}: {e}\n'); return
    sent_s1 = False
    def burst(label):
        for _ in range(3): s.write(b'V'); s.flush(); time.sleep(0.15)
        f.write(f'{time.monotonic()-t0:8.2f} [host] sent V ({label})\n'); f.flush()
    with open(out,'w') as f:
        while time.monotonic()-t0 < secs:
            t = time.monotonic()-t0
            if not sent_s1 and t >= S1_V_AT:          # session 1: fixed time (periph up, central scanning)
                burst("session1, Order B"); sent_s1 = True
            try:
                trig = vq.get_nowait()
                if trig > 10.0:                        # SCAN-READY after a reboot -> session 2, Order B
                    time.sleep(5.0)   # SETTLE: let the rebooted central be up ~5s before reconnect
                    burst(f"session2 post-reboot SCAN-READY@{trig:.2f}, Order B + 5s settle")
            except queue.Empty:
                pass
            try: ln = s.readline()
            except Exception as e: f.write(f'[readerr {e}]\n'); break
            if ln:
                f.write(f'{time.monotonic()-t0:8.2f} ' + ln.decode('utf-8','replace')); f.flush()
    s.close()

if __name__ == '__main__':
    pport, cport, outdir, tag = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    SECS = 56.0
    vq = queue.Queue()
    print(f"[{tag}] reset CENTRAL first (mode g), periph at +3s; gated -> Order-B s1 AND s2")
    reset(CENTRAL_SN)
    t0 = time.monotonic()
    ts = [threading.Thread(target=central_io, args=(cport, SECS, f'{outdir}/gated8-{tag}-central.log', t0, vq)),
          threading.Thread(target=periph_io,  args=(pport, SECS, f'{outdir}/gated8-{tag}-periph.log',  t0, vq))]
    for t in ts: t.start()
    time.sleep(3.0)
    reset(PERIPH_SN)
    for t in ts: t.join()
    print(f"[{tag}] capture done")
