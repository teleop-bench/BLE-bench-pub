#!/usr/bin/env python3
"""Open-stack CoC duplex split diagnosis (2026-10-06): central 'baseline' vs 'pool64' (controller + host receive
buffers matched to the sink), 7.5 ms, FSU off, ABBA x2 (n=4 each). Uses tools/coc-duplex-fsu.py's measure() and
procedure: flash central, discard the first connection, then per rep open the capture and reset only the peripheral."""
import importlib.util, json, os, statistics, subprocess, sys, time
REPO, S = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location('cdf', os.path.join(REPO, 'tools', 'coc-duplex-fsu.py'))
cdf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cdf)
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
HEX = {'baseline': f'{S}/b/c-baseline/zephyr/zephyr.hex', 'pool64': f'{S}/b/c-pool64/zephyr/zephyr.hex'}
def flash(h, sn):                                             # program, then reset (program leaves the core halted)
    r = cdf.sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: sys.exit(f'flash failed {h}')
    cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)
cdf.flash = flash
caps = f'{S}/caps'; os.makedirs(caps, exist_ok=True)
cdf.flash(f'{S}/b/sink/zephyr/zephyr.hex', PER)
res = []
for i, v in enumerate(['baseline', 'pool64', 'pool64', 'baseline', 'baseline', 'pool64', 'pool64', 'baseline']):
    tag = f'pool-{v}-r{i+1}'
    cdf.flash(HEX[v], CEN); time.sleep(15)                      # discard the first (wedge-prone) connection
    p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(cdf.SECS),
                          f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'], env=dict(os.environ, CAP_OUTDIR=caps),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', PER], timeout=60)
    p.wait(timeout=cdf.SECS + 60)
    r = cdf.measure(caps, tag, 'off', 6, 'open'); r['variant'] = v; res.append(r)
    print(json.dumps({k: r.get(k) for k in ('tag', 'dl', 'ul', 'agg', 'stall', 'reject')}), flush=True)
    with open(f'{S}/results-pool.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
print('\n== split (accepted reps): downlink share = dl / (dl + ul)')
for v in ('baseline', 'pool64'):
    ok = [r for r in res if r['variant'] == v and not r['reject']]
    if ok:
        dl = statistics.mean(r['dl'] for r in ok); ul = statistics.mean(r['ul'] for r in ok)
        print(f'  {v:8s}: downlink {dl:6.1f}  uplink {ul:6.1f}  total {dl+ul:6.1f}  downlink share {100*dl/(dl+ul):4.1f}%  (n={len(ok)})')
