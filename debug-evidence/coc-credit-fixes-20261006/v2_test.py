#!/usr/bin/env python3
"""Credit/queue trace of open CoC duplex (2026-10-06). See PREDICTIONS.md. Same procedure as
tools/coc-duplex-fsu.py: flash the central, discard its first connection; per rep program the sink variant
(halted), open the capture, reset only the peripheral; rates via coc-duplex-fsu.measure()."""
import importlib.util, json, os, re, subprocess, sys, time
REPO, S, B = sys.argv[1], sys.argv[2], sys.argv[3]
spec = importlib.util.spec_from_file_location('cdf', os.path.join(REPO, 'tools', 'coc-duplex-fsu.py'))
cdf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cdf)
CEN, CTTY, PER, PTTY = '1057719509', '/dev/cu.usbmodem0010577195093', '1057794857', '/dev/cu.usbmodem0010577948573'
def flash(h, sn, reset=True):
    r = cdf.sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: sys.exit(f'flash failed {h}')
    if reset: cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)
ARM = {'V': ('c64v2', 's64v2')}
hexf = lambda b: f'{B}/{b}/zephyr/zephyr.hex'
caps = f'{S}/caps'; os.makedirs(caps, exist_ok=True)
F = ('n', 'nofeed', 'cr0', 'ctrlfull', 'hostheld', 'ctrlempty', 'returns', 'ret_cr', 'zero_ep')
def ctrace(path):
    lines = [l for l in open(path, errors='replace') if 'CTRACE n=' in l][5:]   # skip the first 5 s
    if not lines: return None
    tot = {k: 0 for k in F}; crm = []
    for l in lines:
        d = dict(re.findall(r'(\w+)=(\d+)', l.split('CTRACE', 1)[1]))
        for k in F: tot[k] += int(d.get(k, 0))
        crm.append(int(d['cr_mean_x10']) / 10); bufs = int(d['ctrlbufs'])
    n = tot['n'] or 1
    out = {k: round(100 * tot[k] / n, 1) for k in ('nofeed', 'cr0', 'ctrlfull', 'hostheld', 'ctrlempty')}
    out.update(secs=len(lines), cr_mean=round(sum(crm) / len(crm), 1), ctrlbufs=bufs,
               returns_per_s=round(tot['returns'] / len(lines), 1),
               cr_per_return=round(tot['ret_cr'] / max(1, tot['returns']), 1),
               zero_ep_per_s=round(tot['zero_ep'] / len(lines), 1))
    return out
cur = {CEN: None, PER: None}
def put(sn, b, reset=True):
    if cur[sn] != b or not reset: flash(hexf(b), sn, reset); cur[sn] = b
order = ['V', 'V', 'V', 'V']
res = []
for i, a in enumerate(order):
    cb, sb = ARM[a]; tag = f'v2-{a}-r{i+1}'
    if cur[CEN] != cb:
        put(PER, sb); put(CEN, cb); time.sleep(15)            # new central image: discard its first connection
    flash(hexf(sb), PER, reset=False); cur[PER] = sb          # halted until the capture is open
    p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(cdf.SECS),
                          f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'], env=dict(os.environ, CAP_OUTDIR=caps),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); cdf.sh(['nrfutil', 'device', 'reset', '--serial-number', PER], timeout=60)
    p.wait(timeout=cdf.SECS + 60)
    try: r = cdf.measure(caps, tag, 'off', 6, 'open')
    except Exception as e: r = {'tag': tag, 'reject': [f'measure: {e}']}
    r['arm'] = a
    r['ct_cen'] = ctrace(f'{caps}/{tag}-cen.log'); r['ct_per'] = ctrace(f'{caps}/{tag}-per.log')
    res.append(r)
    print(json.dumps({k: r.get(k) for k in ('tag', 'dl', 'ul', 'reject', 'ct_cen', 'ct_per')}), flush=True)
    with open(f'{S}/results-v2.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
