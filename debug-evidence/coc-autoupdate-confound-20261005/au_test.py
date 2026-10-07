#!/usr/bin/env python3
"""Single-variable test (2026-10-05): does the CoC sink's connection-parameter auto-update (prefers 50 ms) raise
SDC CoC 'at 7.5 ms' throughput ~5 s into a run, as suspected for the 2026-08-14 head-to-head? Central: SDC CoC FSU-on
7.5 ms (oneway-crossstack 'max' image). Sinks: identical except BT_GAP_AUTO_UPDATE_CONN_PARAMS (matched-pair verified).
Order on, off, on, off; 40 s captures from boot; per-second receiver throughput."""
import os, re, subprocess, sys, time, statistics, json
REPO, X = sys.argv[1], sys.argv[2]
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
CHEX = sys.argv[3]; SINK = {'au-on': sys.argv[4], 'au-off': sys.argv[5]}
def sh(c, t=180): return subprocess.run(c, capture_output=True, text=True, timeout=t)
def flash(h, sn): sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn]); sh(['nrfutil', 'device', 'reset', '--serial-number', sn], 60)
caps = os.path.join(X, 'caps'); os.makedirs(caps, exist_ok=True)
flash(CHEX, CEN); res = []
for i, arm in enumerate(['au-on', 'au-off', 'au-on', 'au-off']):
    tag = f'{arm}-r{i+1}'; flash(SINK[arm], PER)
    p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), '40', f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                         env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); sh(['nrfutil', 'device', 'reset', '--serial-number', PER], 60); sh(['nrfutil', 'device', 'reset', '--serial-number', CEN], 60); p.wait(timeout=120)
    s = [(float(m.group(1)), int(m.group(2))) for l in open(f'{caps}/{tag}-per.log', errors='ignore') for m in [re.match(r'\s*([\d.]+)\s+SINK rx:\s*(\d+)', l)] if m]
    t0 = next((t for t, v in s if v > 20), None)
    early = [v for t, v in s if t0 and t0 + 1 <= t < t0 + 4.5]; late = [v for t, v in s if t0 and t >= t0 + 15]
    r = {'tag': tag, 'early_1_4s': round(statistics.mean(early), 1) if early else None, 'late_15s_on': round(statistics.mean(late), 1) if late else None,
         'series': [v for t, v in s]}
    res.append(r); print(json.dumps({k: r[k] for k in ('tag', 'early_1_4s', 'late_15s_on')}), flush=True)
json.dump(res, open(os.path.join(X, 'results.json'), 'w'), indent=1)
