#!/usr/bin/env python3
"""Step W: reconnect wedge with the v3 central (initial credits in the connect request). cold = reset both boards
per rep (measured connection = first after central boot); warm = peripheral-only after a discarded first connection."""
import importlib.util, json, os, re, subprocess, sys, time
REPO, S, B = sys.argv[1], sys.argv[2], sys.argv[3]
spec = importlib.util.spec_from_file_location('cdf', os.path.join(REPO, 'tools', 'coc-duplex-fsu.py'))
cdf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cdf)
spec2 = importlib.util.spec_from_file_location('ct', os.path.join(S, 'fix_test.py'))
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
def flash(h, sn, reset=True):
    r = cdf.sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: sys.exit(f'flash failed {h}')
    if reset: cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)
src = open(os.path.join(S, 'fix_test.py')).read()
exec(src[src.index('F = ('):src.index('cur = {')])            # reuse ctrace() parser
caps = f'{S}/caps'; os.makedirs(caps, exist_ok=True)
flash(f'{B}/s64v2/zephyr/zephyr.hex', PER)
plan = [('cold', 6, 'c3-6')] * 6 + [('warm', 6, 'c3-6')] * 4 + [('cold', 20, 'c3-20')] * 4
loaded = None
for i, (mode, u, cb) in enumerate(plan):
    tag = f'w-{mode}{u}-r{i+1}'
    if cb != loaded or (mode == 'warm' and plan[i-1][0] == 'cold'):
        flash(f'{B}/{cb}/zephyr/zephyr.hex', CEN); loaded = cb
        if mode == 'warm': time.sleep(15)                      # discard the first connection
    p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(cdf.SECS),
                          f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'], env=dict(os.environ, CAP_OUTDIR=caps),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', PER], timeout=60)
    if mode == 'cold': cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', CEN], timeout=60)
    p.wait(timeout=cdf.SECS + 60)
    try: r = cdf.measure(caps, tag, 'off', u, 'open', mode == 'cold')
    except Exception as e: r = {'tag': tag, 'reject': [f'measure: {e}']}
    r.update(mode=mode, units=u, ct_cen=ctrace(f'{caps}/{tag}-cen.log'), ct_per=ctrace(f'{caps}/{tag}-per.log'))
    print(json.dumps({k: r.get(k) for k in ('tag', 'dl', 'ul', 'stall', 'reject')}), flush=True)
    with open(f'{S}/results-w.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
