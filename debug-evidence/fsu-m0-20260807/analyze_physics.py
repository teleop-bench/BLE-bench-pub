#!/usr/bin/env python3
"""M0 Phase-4 physics gate — spacing sweep vs completion-floor model.

ESTIMATOR (registered before reading any m0-phys-* data, per the M0 plan's
preregistered gate): per-second central `BLASTC comp=N exkBps=.. cgap_us
[min/avg/max]=a/b/c` lines; keep seconds with comp>0; drop the first 20 kept
seconds (connection setup + FSU negotiation at +3 s + warm-up); floor
estimate per cell = MEDIAN of the remaining per-second cgap avg values
(cgap min reported alongside, not gated). Model: pair = 1048 + 44 + 2*s us
at 2M/244 B -> 1392/1292/1232/1196 for s=150/100/70/52.

GATES (fsu-m0-plan.md rev 2, Phase 4 item 2): least-squares slope of
floor-vs-spacing within 2.0 +/- 0.3 us/us AND every per-point residual from
the fitted line < 15 us. Corroboration only — the qualified on-air capture
remains the physical gate.

Secondary: peripheral rxkBps (KiB/s, /1024) per cell, same-window median.
"""
import re, os, sys

EV = os.path.dirname(os.path.abspath(__file__))
STEPS = [150, 100, 70, 52]
MODEL = {s: 1048 + 44 + 2 * s for s in STEPS}

def cell(v):
    raw = open(f'{EV}/m0-phys-f{v}-central.log', 'rb').read().decode('utf-8', 'replace')
    rows = []
    for m in re.finditer(r'BLASTC comp=(\d+) exkBps=\d+ cgap_us\[min/avg/max\]=(\d+)/(\d+)/(\d+)', raw):
        comp, mn, avg, mx = map(int, m.groups())
        if comp > 0:
            rows.append((mn, avg))
    kept = rows[20:]
    if not kept:
        return None
    avgs = sorted(a for _, a in kept)
    mins = sorted(m for m, _ in kept)
    p = open(f'{EV}/m0-phys-f{v}-periph.log', 'rb').read().decode('utf-8', 'replace')
    rx = sorted(int(m.group(1)) for m in re.finditer(r'rxkBps=(\d+)', p) if int(m.group(1)) > 10)
    fsu = re.search(r'FSU: updated status=0x00 spacing=(\d+) us', raw)
    return dict(n=len(kept),
                floor=avgs[len(avgs)//2],
                cgap_min=mins[len(mins)//2],
                rx_med=rx[len(rx)//2] if rx else 0,
                spacing_confirmed=int(fsu.group(1)) if fsu else None)

def main():
    pts = []
    for s in STEPS:
        c = cell(s)
        if c is None:
            print(f's={s}: NO DATA — gate cannot run'); sys.exit(1)
        conf = c['spacing_confirmed']
        note = ''
        if s == 150:
            note = '(control: no-change, no event expected)' if conf is None else f'(unexpected event {conf})'
        elif conf != s:
            note = f'CONFIRM-MISMATCH got {conf}'
        print(f's={s:3d}  model={MODEL[s]}  floor_med={c["floor"]}  cgap_min_med={c["cgap_min"]}  '
              f'rx_med={c["rx_med"]} KiB/s  n={c["n"]}  {note}')
        pts.append((s, c['floor']))
    n = len(pts)
    sx = sum(p[0] for p in pts); sy = sum(p[1] for p in pts)
    sxx = sum(p[0]**2 for p in pts); sxy = sum(p[0]*p[1] for p in pts)
    slope = (n*sxy - sx*sy) / (n*sxx - sx*sx)
    icept = (sy - slope*sx) / n
    resid = [(s, y - (icept + slope*s)) for s, y in pts]
    rmax = max(abs(r) for _, r in resid)
    print(f'fit: floor = {icept:.1f} + {slope:.3f}*spacing ; residuals ' +
          ' '.join(f'{s}:{r:+.1f}' for s, r in resid))
    slope_ok = 1.7 <= slope <= 2.3
    resid_ok = rmax < 15
    print(f'GATE slope in [1.7,2.3]: {"PASS" if slope_ok else "FAIL"} ({slope:.3f})')
    print(f'GATE residuals < 15us:   {"PASS" if resid_ok else "FAIL"} (max {rmax:.1f})')
    print('PHYSICS GATE: ' + ('PASS' if slope_ok and resid_ok else 'FAIL'))

if __name__ == '__main__':
    main()
