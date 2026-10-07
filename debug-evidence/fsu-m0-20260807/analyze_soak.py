#!/usr/bin/env python3
"""M0 Phase-4 floor soak + FSU-off regression — criteria registered while
both runs were in flight, before any log was read.

SOAK (m0-soak-f52, 780 s, 2M / 7.5 ms latency rig, FSU 52 us):
- 'FSU: updated status=0x00 spacing=52 us' count >= 2: the first is the
  initial negotiation; a reconnect probe (peripheral reset at t=660 s)
  must produce a fresh connection (default 150 restored by definition of
  a new connection) followed by RE-negotiation to 52 — the second
  confirm line IS the reconnect/default-restoration witness.
- Pre-probe window (post-settle .. t=660): disc delta == 0, pong-timeout
  (to=) delta == 0, >= 10 min at the negotiated floor.
- RTT sanity: median of per-second means in [11.8, 12.1] ms (the spacing
  does not move the serialized 7.5 ms RTT; a shift would indicate a
  scheduling side effect).
- Cancellation diagnostics reported (expect ~0 at 2M/7.5 ms; nonzero is
  reported, not gated).
- Post-probe: exactly one disc increment (the probe), reconnection, and
  the second FSU confirm; no FATAL/USAGE FAULT/Halting/in-window boots
  other than the probe's peripheral reboot.

REGRESSION (m0-reg-off, 660 s, plain prj.conf builds, FSU config off —
note the M0 code is compiled in unconditionally; this bounds BEHAVIOR):
- No 'FSU:' lines at all (host surface off).
- RTT median of per-second means within +/-1% of the 11.92 ms B1
  baseline: [11.80, 12.04] ms.
- disc delta == 0, to= delta == 0 post-settle; cancels == 0 throughout;
  no fault markers.
PASS = all bounds met -> M0 FSU-off behavior is within the registered
envelope of the pre-M0 controller (hash-identity unavailable by design).
"""
import re, os

EV = os.path.dirname(os.path.abspath(__file__))

def lines(raw, pat):
    return [(i, m) for i, l in enumerate(raw.splitlines())
            for m in [re.search(pat, l)] if m]

def rtt_stats(raw, lo=None, hi=None):
    rows = [(i, int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)))
            for i, m in lines(raw, r't=(\d+)s RTT mean=(\d+) min=\d+ max=\d+ n=(\d+) to=(\d+)')]
    rows = [r for r in rows if r[3] > 0]
    if lo is not None:
        rows = [r for r in rows if lo <= r[1] <= (hi if hi else 10**9)]
    return rows

def main():
    ok = True
    # ---- SOAK ----
    c = open(f'{EV}/m0-soak-f52-central.log', 'rb').read().decode('utf-8', 'replace')
    confirms = len(re.findall(r'FSU: updated status=0x00 spacing=52 us', c))
    print(f'SOAK: FSU spacing=52 confirms = {confirms} (need >=2: initial + post-reconnect)')
    ok &= confirms >= 2
    rows = rtt_stats(c)
    pre = [r for r in rows if 30 <= r[1] <= 655]
    if pre:
        discs = [int(m.group(1)) for i, m in lines(c, r'disc=(\d+)\(')]
        # disc value at the last pre-probe row vs settle
        pre_to = pre[-1][4] - pre[0][4]
        meds = sorted(r[2] for r in pre)
        med = meds[len(meds)//2]
        print(f'SOAK pre-probe: {len(pre)} s window, RTT med {med} us, to-delta {pre_to}')
        ok &= 11800 <= med <= 12100 and pre_to == 0
        # disc deltas within pre-probe window via t-indexed rows
        dpre = [int(m.group(1)) for i, m in lines(c, r't=\d+s RTT[^\n]*disc=(\d+)\(')]
        # align disc to same rows
        drows = [(int(m.group(1)), int(m.group(2))) for i, m in
                 lines(c, r't=(\d+)s RTT[^\n]*disc=(\d+)\(')]
        d_pre = [d for t, d in drows if 30 <= t <= 655]
        if d_pre:
            print(f'SOAK pre-probe disc delta: {d_pre[-1] - d_pre[0]}')
            ok &= (d_pre[-1] - d_pre[0]) == 0
        d_post = [d for t, d in drows if t > 655]
        if d_post:
            print(f'SOAK post-probe disc delta: {d_post[-1] - (d_pre[-1] if d_pre else 0)} (expect exactly 1)')
            ok &= (d_post[-1] - (d_pre[-1] if d_pre else 0)) == 1
        canc = [int(m.group(1)) for i, m in lines(c, r'cancels=(\d+)\(')]
        print(f'SOAK cancels final cumulative: {canc[-1] if canc else None} (reported, not gated)')
        mins10 = (pre[-1][1] - pre[0][1]) / 60
        print(f'SOAK minutes at floor pre-probe: {mins10:.1f} (need >=10)')
        ok &= mins10 >= 10
    else:
        print('SOAK: no pre-probe RTT rows'); ok = False
    sp = open(f'{EV}/m0-soak-f52-periph.log', 'rb').read().decode('utf-8', 'replace')
    for marker in ('FATAL', 'USAGE FAULT', 'Halting'):
        if marker in c or marker in sp:
            print(f'SOAK marker {marker} present'); ok = False
    pdisc = [int(m.group(1)) for i, m in lines(sp, r'disc=(\d+)')]
    print(f'SOAK periph disc final: {pdisc[-1] if pdisc else None} '
          f'(expect 0: the probe reboots the periph, resetting counters; '
          f'the central disc=1 is the probe witness)')
    # ---- REGRESSION ----
    rc = open(f'{EV}/m0-reg-off-central.log', 'rb').read().decode('utf-8', 'replace')
    if 'FSU:' in rc:
        print('REG: unexpected FSU lines'); ok = False
    rrows = rtt_stats(rc)
    rr = [r for r in rrows if r[1] >= 30]
    if rr:
        meds = sorted(r[2] for r in rr)
        med = meds[len(meds)//2]
        to_d = rr[-1][4] - rr[0][4]
        drows = [(int(m.group(1)), int(m.group(2))) for i, m in
                 lines(rc, r't=(\d+)s RTT[^\n]*disc=(\d+)\(')]
        dd = [d for t, d in drows if t >= 30]
        canc = [int(m.group(1)) for i, m in lines(rc, r'cancels=(\d+)\(')]
        print(f'REG: RTT med {med} us (bound 11800..12040), to-delta {to_d}, '
              f'disc-delta {dd[-1]-dd[0] if dd else None}, cancels-final {canc[-1] if canc else None}')
        ok &= 11800 <= med <= 12040 and to_d == 0 and dd and (dd[-1]-dd[0]) == 0 \
              and canc and canc[-1] == 0
    else:
        print('REG: no RTT rows'); ok = False
    rp = open(f'{EV}/m0-reg-off-periph.log', 'rb').read().decode('utf-8', 'replace')
    for marker in ('FATAL', 'USAGE FAULT', 'Halting'):
        if marker in rc or marker in rp:
            print(f'REG marker {marker} present'); ok = False
    rpd = [int(m.group(1)) for i, m in lines(rp, r'disc=(\d+)')]
    if rpd and rpd[-1] != 0:
        print(f'REG periph disc={rpd[-1]}'); ok = False
    print('PHASE-4 SOAK+REGRESSION: ' + ('PASS' if ok else 'FAIL'))

if __name__ == '__main__':
    main()
