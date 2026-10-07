#!/usr/bin/env python3
"""nRF54 load-ramp analysis — the published computation (rev 2).

RTT: segment the central log by LOADRAMP target markers; within each stage
drop the first 5 per-second RTT lines (settle), weighted-mean the RTT by n,
take max.

Achieved load (rev 2 — deterministic stage windows, replacing the rev-1
value-proximity bands that cross-contaminated adjacent stages): peripheral
`P t=<s> ... blkkBps=<v>` samples are placed on the peripheral clock.
Onset rule (exact): the timestamp of the FIRST of three consecutive samples
with blkkBps > 10 marks the start of stage 1 (target 25). Stage k
(k=0..6, targets 0,25,50,75,100,125,150) occupies
[onset + 60*(k-1), onset + 60*k); 5 s trimmed from each boundary; achieved
= mean of samples inside the trimmed window. Timestamps must be monotonic;
each trimmed window must hold >= 40 of the ~50 expected samples.

KiB/s throughout (divisor 1024; firmware counters are /1024).
Single cycle per arm: HYPOTHESIS-GENERATING.
"""
import re, csv, os
EV = os.path.dirname(os.path.abspath(__file__))
TARGETS = [0, 25, 50, 75, 100, 125, 150]
rows = [['arm', 'target_KiBps', 'achieved_KiBps', 'mean_rtt_ms', 'max_rtt_ms', 'secs', 'ach_n']]
for arm in ('open', 'sdc', 'sdcfsu'):
    c = open(f'{EV}/loadramp-{arm}-central.log', 'rb').read().decode(errors='replace')
    p = open(f'{EV}/loadramp-{arm}-periph.log', 'rb').read().decode(errors='replace')
    # peripheral samples on the peripheral clock
    samp = [(int(m.group(1)), int(m.group(2)))
            for m in re.finditer(r'P t=(\d+)s [^\r\n]*?blkkBps=(\d+)', p)]
    ts = [t for t, _ in samp]
    assert all(b > a for a, b in zip(ts, ts[1:])), f'{arm}: non-monotonic periph timestamps'
    onset = None
    for i in range(len(samp) - 2):
        if all(samp[i + j][1] > 10 for j in range(3)):
            onset = samp[i][0]
            break
    assert onset is not None, f'{arm}: no onset (3 consecutive blkkBps>10) found'
    # central RTT stages (unchanged from rev 1)
    cur = None; stages = []
    for l in c.splitlines():
        m = re.match(r'LOADRAMP: target=(\d+) KBps', l)
        if m:
            cur = {'t': int(m.group(1)), 'r': []}; stages.append(cur); continue
        if cur and 'RTT mean=' in l:
            mm = re.search(r'mean=(\d+) min=(\d+) max=(\d+) n=(\d+)', l)
            if mm and int(mm.group(4)) > 0:
                cur['r'].append(tuple(map(int, mm.groups())))
    assert [st['t'] for st in stages[:7]] == TARGETS, f'{arm}: unexpected stage order'
    for k, st in enumerate(stages[:7]):
        rs = st['r'][5:]
        if not rs:
            continue
        tn = sum(r[3] for r in rs); mean = sum(r[0] * r[3] for r in rs) / max(tn, 1)
        w0 = onset + 60 * (k - 1) + 5
        w1 = onset + 60 * k - 5
        band = [b for t, b in samp if w0 <= t < w1]
        assert len(band) >= 40, f'{arm} stage {st["t"]}: only {len(band)} samples in window'
        ach = sum(band) / len(band)
        rows.append([arm, st['t'], f'{ach:.1f}', f'{mean/1000:.2f}',
                     f'{max(r[2] for r in rs)/1000:.1f}', len(rs), len(band)])
with open(f'{EV}/loadramp.csv', 'w', newline='') as f:
    csv.writer(f).writerows(rows)
print('loadramp.csv written,', len(rows) - 1, 'rows')
for r in rows[1:]:
    print(r)
