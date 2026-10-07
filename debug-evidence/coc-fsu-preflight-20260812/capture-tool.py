#!/usr/bin/env python3
# pyserial capture — asserts DTR (the nRF DK J-Link console gates output on DTR;
# a raw os.open without DTR reads nothing after a power cycle).
import sys, time, serial

OUTDIR = "/tmp/scratch"

dur = float(sys.argv[1])
specs = [p.rsplit(':',1) for p in sys.argv[2:]]
ports, bufs, outs = {}, {}, {}
for path,label in specs:
    s = serial.Serial(path, 115200, timeout=0)
    s.dtr = True; s.rts = True
    ports[label] = s; bufs[label] = b''
    outs[label] = open(f"{OUTDIR}/{label}.log", 'w')

t0 = time.time()
while time.time() - t0 < dur:
    got = False
    for label,s in ports.items():
        try:
            data = s.read(4096)
        except Exception:
            data = b''
        if data:
            got = True
            bufs[label] += data
            while b'\n' in bufs[label]:
                line, bufs[label] = bufs[label].split(b'\n',1)
                ts = time.time() - t0
                outs[label].write(f"{ts:8.3f}  {line.decode('utf-8','replace').rstrip()}\n")
                outs[label].flush()
    if not got:
        time.sleep(0.05)
for s in ports.values(): s.close()
for o in outs.values(): o.close()
print("capture done", dur, "s")
