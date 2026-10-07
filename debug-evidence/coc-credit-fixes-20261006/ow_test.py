#!/usr/bin/env python3
"""One-way CoC FSU at 7.5 ms with a batched-credit sink (see PREDICTIONS-oneway.md). Central reflashed per arm
(first connection discarded), peripheral-only resets; rates via coc-duplex-fsu.measure() (its 'uplink stalled'
reject is expected: the uplink is off), FSU-held checked on the downlink series."""
import importlib.util, json, os, re, statistics, subprocess, sys, time
REPO, S, B = sys.argv[1], sys.argv[2], sys.argv[3]
spec = importlib.util.spec_from_file_location('cdf', os.path.join(REPO, 'tools', 'coc-duplex-fsu.py'))
cdf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cdf)
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
def flash(h, sn, reset=True):
    r = cdf.sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: sys.exit(f'flash failed {h}')
    if reset: cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)
caps = f'{S}/caps'; os.makedirs(caps, exist_ok=True)
flash(f'{B}/s1w/zephyr/zephyr.hex', PER)
loaded, res = None, []
for i, arm in enumerate(['off', 'on', 'on', 'off'] * 2):
    tag = f'ow-{arm}-r{i+1}'
    if arm != loaded:
        flash(f'{B}/c1w-{arm}/zephyr/zephyr.hex', CEN); loaded = arm; time.sleep(15)
    p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(cdf.SECS),
                          f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'], env=dict(os.environ, CAP_OUTDIR=caps),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', PER], timeout=60)
    p.wait(timeout=cdf.SECS + 60)
    r = cdf.measure(caps, tag, arm, 6, 'open')
    r['reject'] = [x for x in r['reject'] if not x.startswith('uplink stalled')]
    if arm == 'on' and 'dl' in r:
        per = open(f'{caps}/{tag}-per.log', errors='replace').read().splitlines()
        ser = [(cdf.ts(l), int(m.group(1))) for l in per for m in [re.search(r'SINK rx: (\d+) KB/s', l)] if m and (cdf.ts(l) or -1) >= r['onset_s'] + 2]
        held, why, det = cdf.fsu_held(ser, settle=r['onset_s'] + 2)
        if not held: r['reject'].append(f'dl not held: {why} {det}')
    r['arm'] = arm; res.append(r)
    print(json.dumps({k: r.get(k) for k in ('tag', 'spacing', 'dl', 'dl_line_median', 'reject')}), flush=True)
    with open(f'{S}/results-ow.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
ok = {a: [r['dl'] for r in res if r['arm'] == a and not r['reject']] for a in ('off', 'on')}
if ok['off'] and ok['on']:
    mo, mn = statistics.mean(ok['off']), statistics.mean(ok['on'])
    print(f"\nFSU off {mo:.1f} (n={len(ok['off'])})  on {mn:.1f} (n={len(ok['on'])})  gain {100*(mn-mo)/mo:+.1f}%")
