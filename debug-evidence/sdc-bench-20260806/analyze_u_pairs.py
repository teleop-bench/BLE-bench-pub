#!/usr/bin/env python3
"""U0/U1 paired FSU throughput analysis — the exact published computation.

Convention (preregistration note): per run, take the peripheral's per-second
rxkBps values, keep values >10 (drops pre-connection zeros), then DROP THE
FIRST 15 kept samples (connection settle; the runner additionally excluded a
45 s warm-up before capture). Estimator: mean of remaining per-second kBps
values (kB = 1024 bytes, receiver counter deltas). Delta per pair =
(U1-U0)/U0. CI: within-session paired t-interval across the 6 pairs
(t_{0.975,5} = 2.571) — ONE board pair, one session; NOT a device-population
interval. Windowing differences of a few samples move the result by ~0.03
points (independently reproduced at 15.00 [12.34, 17.66]).
"""
import re, statistics as st, csv, sys, os
EV = os.path.dirname(os.path.abspath(__file__))
SEQ = 'AB BA BA AB AB BA'.split()
rows = []
deltas = []
for pn, pair in enumerate(SEQ, 1):
    vals = {}
    for arm in pair:
        txt = open(f'{EV}/u-pair{pn}-{arm}-periph.log', 'rb').read().decode(errors='replace')
        v = [int(m.group(1)) for m in re.finditer(r'rxkBps=(\d+)', txt)]
        v = [x for x in v if x > 10][15:]
        vals[arm] = (sum(v) / len(v), len(v))
    d = (vals['B'][0] - vals['A'][0]) / vals['A'][0] * 100
    deltas.append(d)
    rows.append([pn, pair, f"{vals['A'][0]:.2f}", vals['A'][1],
                 f"{vals['B'][0]:.2f}", vals['B'][1], f"{d:.2f}"])
m = st.mean(deltas); sd = st.stdev(deltas); half = 2.571 * sd / (6 ** 0.5)
with open(f'{EV}/u-pairs.csv', 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['pair', 'order', 'U0_kBps_mean', 'U0_n_samples',
                'U1_kBps_mean', 'U1_n_samples', 'delta_pct'])
    w.writerows(rows)
    w.writerow([])
    w.writerow(['paired_mean_pct', f'{m:.2f}'])
    w.writerow(['ci95_low_pct', f'{m-half:.2f}'])
    w.writerow(['ci95_high_pct', f'{m+half:.2f}'])
    w.writerow(['note', 'within-session paired t-CI, one board pair'])
print(f'{m:.2f}% [{m-half:.2f}, {m+half:.2f}] -> u-pairs.csv')
