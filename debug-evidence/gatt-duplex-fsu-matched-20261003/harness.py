#!/usr/bin/env python3
"""GATT echo duplex FSU, matched arms, held-verified, post-onset window, ABBA (2026-10-03).

central = z54-lat-central tput-open;fsu-open;hh-fsu52 (on) | hh-fsu150 (off)  [check_matched_pair: MATCHED]
periph  = z54-lat-periph tput-echo-open;fsu-open, AUTO_UPDATE=n, floor 52      [shared, flashed once]
aggregate = central exkBps (echo B->A) + periph rxkBps (blast A->B), medians over the window
window = from the central's FSU-completion line + 2 s to end of capture (both arms run the FSU procedure)
"""
import json, os, re, statistics, subprocess, sys, time
REPO = '<REPO>'
sys.path.insert(0, REPO + '/tools')
from verify_run import fsu_held

D = os.path.dirname(os.path.abspath(__file__))
CAPS = D + ('/caps-smoke' if os.environ.get('SMOKE') else '/caps'); os.makedirs(CAPS, exist_ok=True)
CEN, CTTY = '1057719509', '/dev/cu.usbmodem0010577195093'
PER, PTTY = '1057794857', '/dev/cu.usbmodem0010577948573'
SECS = 45
ORDER = ['off', 'on', 'on', 'off', 'off', 'on', 'on', 'off']      # ABBA x2 -> n=4 per arm
INTERVALS = [(6, 7500), (20, 25000)]
if os.environ.get('SMOKE'):                                        # one rep per arm per interval
    ORDER = ['off', 'on']

def run(cmd, t=180):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=t)

def flash(hexpath, sn):
    r = run(['nrfutil', 'device', 'program', '--firmware', hexpath, '--serial-number', sn])
    if r.returncode: raise SystemExit(f'flash failed {hexpath}: {r.stderr[-300:]}')

def after_last_boot(path):
    lines = open(path, errors='ignore').read().splitlines()
    idx = max([i for i, l in enumerate(lines) if 'Booting Zephyr' in l] or [0])
    return lines[idx:]

def ts(l):
    m = re.match(r'\s*([0-9.]+)\s', l); return float(m.group(1)) if m else None

def measure(tag, arm, iv_us):
    cen = after_last_boot(f'{CAPS}/{tag}-cen.log'); per = after_last_boot(f'{CAPS}/{tag}-per.log')
    r = {'tag': tag, 'arm': arm, 'interval_us': iv_us, 'reject': []}
    # FSU-on: the controller reports the change ('FSU: updated ... spacing=52').
    # FSU-off: requesting 150 on a link already at 150 is a no-op, so the controller sends no
    # 'updated' event; require the request line (rc=0) and no 52 us anywhere, and time from it.
    upd = [(ts(l), int(re.search(r'spacing=(\d+)', l).group(1))) for l in cen if 'FSU: updated' in l and 'spacing=' in l]
    req = [(ts(l), l) for l in cen if 'FSU: request' in l]
    if arm == 'on':
        if not upd: r['reject'].append('no FSU completion line'); return r
        onset, spacing = upd[-1]
        if spacing != 52: r['reject'].append(f'spacing={spacing} != 52')
    else:
        if any(sp == 52 for _, sp in upd): r['reject'].append('off-arm reached spacing=52')
        okreq = [t for t, l in req if '[150..150]' in l and 'rc=0' in l]
        if not okreq: r['reject'].append('no FSU request [150..150] rc=0 line'); return r
        onset, spacing = (upd[-1] if upd else (okreq[-1], 150))
    r['onset_s'] = onset; r['spacing'] = spacing
    ivs = {int(m.group(1)) for l in cen for m in [re.search(r'interval=(\d+)', l)] if m}
    r['intervals_seen'] = sorted(ivs)
    if ivs != {iv_us}: r['reject'].append(f'interval {sorted(ivs)} != {iv_us}')
    discs = {m.group(1) for l in per for m in [re.search(r'disc=(\d+)', l)] if m}
    if discs - {'0'}: r['reject'].append(f'disconnects {discs}')
    ex = [(ts(l), int(m.group(1))) for l in cen for m in [re.search(r'exkBps=(\d+)', l)] if m]
    rx = [(ts(l), int(m.group(1))) for l in per for m in [re.search(r'rxkBps=(\d+)', l)] if m]
    w0 = onset + 2.0
    exw = [k for t, k in ex if t >= w0]; rxw = [k for t, k in rx if t >= w0]
    if len(exw) < 15 or len(rxw) < 15: r['reject'].append(f'short window ex={len(exw)} rx={len(rxw)}'); return r
    r['ex_med'] = statistics.median(exw); r['rx_med'] = statistics.median(rxw)
    r['agg'] = r['ex_med'] + r['rx_med']
    for name, ser in (('ex', ex), ('rx', rx)):
        held, why, det = fsu_held([(t, k) for t, k in ser if t >= w0], settle=w0)
        r[f'held_{name}'] = held
        if arm == 'on' and not held: r['reject'].append(f'{name} not held: {why} {det}')
    return r

def main():
    S = D
    flash(f'{S}/p-echo/zephyr/zephyr.hex', PER)
    results = []
    for units, iv_us in INTERVALS:
        for i, arm in enumerate(ORDER):
            tag = f'dx{units}_{arm}_r{i+1}'
            flash(f'{S}/c-{arm}-{units}/zephyr/zephyr.hex', CEN)
            p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS), f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                                 env=dict(os.environ, CAP_OUTDIR=CAPS), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            run(['nrfutil', 'device', 'reset', '--serial-number', PER]); run(['nrfutil', 'device', 'reset', '--serial-number', CEN])
            p.wait(timeout=SECS + 60)
            r = measure(tag, arm, iv_us); results.append(r)
            print(json.dumps({k: r.get(k) for k in ('tag', 'spacing', 'onset_s', 'ex_med', 'rx_med', 'agg', 'reject')}), flush=True)
            with open(f'{D}/results{"-smoke" if os.environ.get("SMOKE") else ""}.jsonl', 'a') as f: f.write(json.dumps(r) + '\n')
    print('\n=== SUMMARY (accepted reps only) ===')
    for units, iv_us in INTERVALS:
        rows = {a: [r['agg'] for r in results if r['interval_us'] == iv_us and r['arm'] == a and not r['reject']] for a in ('off', 'on')}
        rej = sum(1 for r in results if r['interval_us'] == iv_us and r['reject'])
        if len(rows['off']) >= 2 and len(rows['on']) >= 2:
            mo, mn = statistics.mean(rows['off']), statistics.mean(rows['on'])
            sep = 'strict non-overlap' if min(rows['on']) > max(rows['off']) or max(rows['on']) < min(rows['off']) else 'ranges overlap'
            print(f'{iv_us/1000:>5} ms  off {mo:6.1f} (n={len(rows["off"])}, {min(rows["off"])}-{max(rows["off"])})  '
                  f'on {mn:6.1f} (n={len(rows["on"])}, {min(rows["on"])}-{max(rows["on"])})  delta {100*(mn-mo)/mo:+.1f}%  {sep}  rejected={rej}')
        else:
            print(f'{iv_us/1000:>5} ms  insufficient accepted reps {rows} rejected={rej}')

if __name__ == '__main__':
    main()
