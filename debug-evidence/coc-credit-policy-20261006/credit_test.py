#!/usr/bin/env python3
"""One-way CoC FSU at 7.5 ms: recipe sink (per-segment credits) vs the same sink with CONFIG_APP_CREDIT_BATCH=y.
Images built exactly as tools/oneway-fsu.py builds the 'max' CoC recipe; per rep: flash if changed, open capture,
reset both boards, 32 s; throughput via oneway-fsu.measure() (median per-second line) plus the cum_total slope."""
import importlib.util, json, os, re, statistics, subprocess, sys, time
REPO, S, NCS = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.join(REPO, 'tools'))
spec = importlib.util.spec_from_file_location('ow', os.path.join(REPO, 'tools', 'oneway-fsu.py'))
ow = importlib.util.module_from_spec(spec); spec.loader.exec_module(ow)
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
B = f'{S}/b'; U = 6
def d(st, name): return f'{B}/{st}/{name}'
def zd(st, name, app): return ow.zdir(d(st, name), app, st)
cen_app, sink_app = ow.APPS['coc']
if '--skip-build' not in sys.argv:
    for st in ('open', 'sdc'):
        rc = ow.RECIPE[('coc', st)]
        for arm in ('off', 'on'):
            ow.build(cen_app, d(st, f'c-{arm}'), ';'.join(x for x in (rc['cen'], rc[arm]) if x),
                     rc['cen_d'] + [f'CONFIG_APP_CONN_INT_UNITS={U}'] + ow.profile_flags('max', 'coc', st, 'cen'), st, NCS)
        sf = rc['sink_d'] + ow.profile_flags('max', 'coc', st, 'sink')
        ow.build(sink_app, d(st, 'sink-seg'), rc['sink'], sf, st, NCS)
        ow.build(sink_app, d(st, 'sink-batch'), rc['sink'], sf + ['CONFIG_APP_CREDIT_BATCH=y'], st, NCS)
        print(f'built {st}', flush=True)
gate = []
for st in ('open', 'sdc'):
    for a, b, allow in ((f'c-on', f'c-off', 'CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US'), ('sink-batch', 'sink-seg', 'CONFIG_APP_CREDIT_BATCH')):
        app = cen_app if a.startswith('c-') else sink_app
        r = ow.sh([sys.executable, os.path.join(REPO, 'tools', 'check_matched_pair.py'), os.path.join(zd(st, a, app), '.config'),
                   os.path.join(zd(st, b, app), '.config'), '--allow', allow], timeout=60)
        line = (r.stdout.strip().splitlines() or ['?'])[-1]; gate.append(f'{st} {a} vs {b}: {line}')
        if r.returncode: sys.exit('GATE FAIL ' + gate[-1])
    pub = os.path.join(REPO, 'debug-evidence/oneway-crossstack-20261005/firmware', f'max-coc-{st}-sink.hex')
    mine = os.path.join(zd(st, 'sink-seg', sink_app), 'zephyr.hex')
    same = os.path.exists(pub) and open(pub, 'rb').read() == open(mine, 'rb').read()
    gate.append(f'{st} default sink byte-identical to published max-coc-{st}-sink.hex: {same}')
open(f'{S}/design-gate.txt', 'w').write('\n'.join(gate) + '\n'); print('\n'.join(gate), flush=True)
caps = f'{S}/caps'; os.makedirs(caps, exist_ok=True)
cur = {CEN: None, PER: None}
def put(h, sn):
    if cur[sn] != h:
        if not ow.flash(h, sn): sys.exit(f'flash failed {h}')
        cur[sn] = h
def slope(per_log, start):
    pts = [(float(m.group(1)), int(m.group(2))) for l in open(per_log, errors='ignore')
           for m in [re.search(r'^\s*([0-9.]+)\s+SINK rx:.*cum_total=(\d+) B', l)] if m and float(m.group(1)) >= start]
    return round((pts[-1][1] - pts[0][1]) / 1024 / (pts[-1][0] - pts[0][0]), 1) if len(pts) >= 5 else None
cells = [(st, pol, arm) for st in ('open', 'sdc') for pol in ('seg', 'batch') for arm in ('off', 'on')]
for rnd in range(4):
    order = cells if rnd % 2 == 0 else cells[::-1]
    for st, pol, arm in order:
        tag = f'cr-{st}-{pol}-{arm}-r{rnd+1}'
        put(os.path.join(zd(st, f'sink-{pol}', sink_app), 'zephyr.hex'), PER)
        put(os.path.join(zd(st, f'c-{arm}', cen_app), 'zephyr.hex'), CEN)
        p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(ow.SECS),
                              f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'], env=dict(os.environ, CAP_OUTDIR=caps),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.5)
        for sn in (PER, CEN): ow.sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)
        p.wait(timeout=ow.SECS + 60)
        r = ow.measure(caps, tag, 'coc', st, arm, U); r.update(policy=pol, round=rnd + 1)
        r['kbps_slope'] = slope(f'{caps}/{tag}-per.log', r['window_start']) if r.get('window_start') else None
        print(json.dumps({k: r.get(k) for k in ('tag', 'spacing', 'kbps', 'kbps_slope', 'reject')}), flush=True)
        with open(f'{S}/results.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
