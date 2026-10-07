#!/usr/bin/env python3
"""H5 (2026-10-06): sink uplink pool 64 (default) vs 16, central baseline, 7.5 ms, FSU off, ABBA x2. The central is
flashed once and its first connection discarded; each rep flashes the sink variant (a peripheral reset), opens the
capture first, and the measured connection is a same-boot reconnection."""
import importlib.util, json, os, statistics, subprocess, sys, time
REPO, S = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location('cdf', os.path.join(REPO, 'tools', 'coc-duplex-fsu.py'))
cdf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cdf)
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
def flash(h, sn, reset=True):
    r = cdf.sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: sys.exit(f'flash failed {h}')
    if reset: cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)
SINK = {'pool64': f'{S}/b/sink-default64/zephyr/zephyr.hex', 'pool16': f'{S}/b/sink-pool16/zephyr/zephyr.hex'}
caps = f'{S}/caps'; os.makedirs(caps, exist_ok=True)
flash(SINK['pool64'], PER); flash(f'{S}/b/c-baseline/zephyr/zephyr.hex', CEN); time.sleep(15)   # discard first connection
res = []
for i, v in enumerate(['pool64', 'pool16', 'pool16', 'pool64', 'pool64', 'pool16', 'pool16', 'pool64']):
    tag = f'sink-{v}-r{i+1}'
    flash(SINK[v], PER, reset=False)                            # program leaves it halted; reset after capture opens
    p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(cdf.SECS),
                          f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'], env=dict(os.environ, CAP_OUTDIR=caps),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', PER], timeout=60)
    p.wait(timeout=cdf.SECS + 60)
    r = cdf.measure(caps, tag, 'off', 6, 'open'); r['variant'] = v; res.append(r)
    print(json.dumps({k: r.get(k) for k in ('tag', 'dl', 'ul', 'agg', 'stall', 'reject')}), flush=True)
    with open(f'{S}/results-sink.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
print('\n== split (accepted reps): downlink share = dl / (dl + ul)')
for v in ('pool64', 'pool16'):
    ok = [r for r in res if r['variant'] == v and not r['reject']]
    if ok:
        dl = statistics.mean(r['dl'] for r in ok); ul = statistics.mean(r['ul'] for r in ok)
        print(f'  {v:8s}: downlink {dl:6.1f}  uplink {ul:6.1f}  total {dl+ul:6.1f}  downlink share {100*dl/(dl+ul):4.1f}%  (n={len(ok)})')
