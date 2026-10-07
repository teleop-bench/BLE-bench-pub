#!/usr/bin/env python3
"""Clean GATT one-way FSU vs interval — CONFOUND-FREE re-run.

Fixes vs the superseded sdc/open sweeps:
  1. Matched off-arm: on/off differ ONLY in requested frame space (52 vs 150 us); same feature package,
     same FSU-capable sink. Enforced at build by tools/check_matched_pair.py (design gate).
  2. Post-FSU measurement window: throughput is the median AFTER FSU negotiates (parsed from the cen-log
     `spacing=` timestamp), never the whole run — the earlier SDC whole-run median blended pre/post-FSU.
  3. Student-t CIs (small n), not 1.96*SE.
Both stacks (open, SDC), 7.5/15/25/37.5/50 ms, counterbalanced, verify_run-gated. `python3 harness.py <reps>`.
"""
import os, sys, re, json, time, subprocess, statistics, math, hashlib, shutil
REPO = '<REPO>'
sys.path.insert(0, REPO + '/tools')
from verify_run import verify
from check_matched_pair import check
D = REPO + '/debug-evidence/gatt-fsu-clean-20260908'
CEN, PER = '1057794857', '1057719509'
CTTY = '/dev/cu.usbmodem0010577948573'; PTTY = '/dev/cu.usbmodem0010577195093'
SECS = 30
GC = '/tmp/gattclean'
UNITS = {7.5: 6, 15: 12, 25: 20, 37.5: 30, 50: 40}
TCRIT = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36, 9: 2.31, 10: 2.26}

def paths(stack, arm, u):
    if stack == 'open':
        cen = f'{GC}/open/{arm}_{u}/zephyr/zephyr.hex'; sink = f'{GC}/open/sink/zephyr/zephyr.hex'
    else:
        cen = f'{GC}/sdc/{arm}_{u}/z54-lat-central/zephyr/zephyr.hex'; sink = f'{GC}/sdc/sink/z54-lat-periph/zephyr/zephyr.hex'
    return cen, sink

def prog(h, d):
    r = subprocess.run(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', d], capture_output=True, timeout=120)
    return r.returncode == 0
def rst(d): subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', d], capture_output=True, timeout=30)

def cfg_of(hexpath):
    """Resolved .config beside the hex actually being flashed (sysbuild-aware)."""
    return os.path.join(os.path.dirname(hexpath), '.config')

def preflight_gate():
    """REFUSE to flash unless every on/off pair is a matched isolation and both sinks are capable +
    auto-update-OFF. Gates the LIVE build configs beside the hexes actually flashed (not a snapshot), so a
    rebuilt/confounded rig cannot bypass it; sys.exit(1) on any failure — the harness flashes nothing until
    the design is clean. (build_gattclean.sh gates the same at build time and also archives these configs.)"""
    allow = ['CONFIG_APP_FSU_MIN_US', 'CONFIG_APP_FSU_MAX_US']; fail = []
    for st in ('open', 'sdc'):
        for iv, u in UNITS.items():
            oncen, sink = paths(st, 'on', u); offcen, _ = paths(st, 'off', u)
            on, off = cfg_of(oncen), cfg_of(offcen)
            if not (os.path.exists(on) and os.path.exists(off)): fail.append(f'{st}_{u}: missing build .config (build first)'); continue
            offenders, intended = check(on, off, allow)
            if offenders: fail.append(f'{st}_{u}: {len(offenders)} confounding symbol(s)')
            if not intended: fail.append(f'{st}_{u}: FSU variable absent (wrong/empty config path)')
    for st in ('open', 'sdc'):
        _, sink = paths(st, 'on', UNITS[7.5]); sk = cfg_of(sink)
        if not os.path.exists(sk): fail.append(f'{st} sink: missing build .config'); continue
        t = open(sk).read()
        if 'CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n' not in t and '# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set' not in t:
            fail.append(f'{st} sink: auto-update not OFF (FSU would revert)')
        if 'SDC_ENABLE_LOWEST_FRAME_SPACE=y' not in t and 'EVENT_IFS_LOW_LAT_US=52' not in t:
            fail.append(f'{st} sink: no FSU floor (on-arm understated)')
    if fail:
        print('DESIGN GATE FAILED — refusing to flash:', flush=True)
        for f in fail: print('  ' + f, flush=True)
        sys.exit(1)
    print('design gate: 10/10 pairs MATCHED + both sinks capable & auto-update-OFF — proceeding', flush=True)

def fsu_onset(cen_log):
    """Timestamp of the last `FSU: updated ... spacing=` line = when the negotiated spacing took effect."""
    t = None
    try:
        for l in open(cen_log):
            m = re.search(r'^\s*([0-9.]+).*FSU: updated.*spacing=(\d+)', l)
            if m: t = float(m.group(1))
    except Exception: pass
    return t

def late_throughput(per_log, onset, win=12.0, settle=1.0, min_pts=5):
    """Median sink-rx over the late window (last `win` s), CLIPPED so it starts strictly after FSU onset
    (+settle) — never straddling the transition (the #2 artifact). Clip, don't reject: a window that would
    start just before onset is moved to onset+settle rather than discarding a valid rep. Reject only if too
    few post-onset points remain (link mostly dead / onset too late to measure a plateau)."""
    s = []
    try:
        for l in open(per_log):
            m = re.search(r'^\s*([0-9.]+)\s+P t=\d+s rxkBps=([0-9]+)', l) or re.search(r'^\s*([0-9.]+)\s+SINK rx:\s*([0-9]+)', l)
            if m: s.append((float(m.group(1)), int(m.group(2))))
    except Exception: pass
    if not s: return (0, None, 0, False)
    tmax = s[-1][0]; start = tmax - win
    if onset is not None: start = max(start, onset + settle)   # clip window to post-FSU-onset
    w = [v for t, v in s if t >= start and v > 0]
    ok = len(w) >= min_pts                                      # enough post-onset points for a robust median
    return (statistics.median(w) if w else 0, round(start, 1), len(w), ok)

def meas(stack, arm, iv, rep):
    u = UNITS[iv]; cen, sink = paths(stack, arm, u)
    tag = f'{stack}_{arm}_{str(iv).replace(".", "p")}_r{rep}'
    caps = D + '/caps'; os.makedirs(caps, exist_ok=True)
    if not (os.path.exists(cen) and os.path.exists(sink)):
        print(f'  {tag}: MISSING FW', flush=True); return None
    if not (prog(sink, PER) and prog(cen, CEN)):
        print(f'  {tag}: FLASH FAILED', flush=True)
        return {'stack': stack, 'arm': arm, 'iv': iv, 'rep': rep, 'kbps': 0, 'onset': None, 'win_n': 0, 'pass': False, 'why': 'flash_failed'}
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS), f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                         env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS + 40)
    except Exception: p.kill()
    per_log = f'{caps}/{tag}-per.log'; cen_log = f'{caps}/{tag}-cen.log'
    onset = fsu_onset(cen_log)
    kbps, wstart, n, win_ok = late_throughput(per_log, onset)
    v = verify(per_log, cen_log, fsu=arm, stack=stack, min_kbps=5)   # arm-aware: off arm isn't required to show spacing=52
    ok = bool(v.get('pass')) and win_ok and kbps > 0
    rej = list(v.get('rejects', []))
    if not win_ok: rej.append(f'window_not_post_fsu(onset={onset})')
    print(f'  {tag}: {kbps:5.0f} KB/s  win>{wstart}  onset={onset}  {"PASS" if ok else "REJECT "+str(rej)}', flush=True)
    return {'stack': stack, 'arm': arm, 'iv': iv, 'rep': rep, 'kbps': kbps, 'onset': onset, 'win_n': n, 'pass': ok, 'why': ';'.join(rej) or 'ok'}

def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    preflight_gate()   # REFUSE to flash a confounded / reverting rig
    cells = [(st, arm, iv) for st in ('open', 'sdc') for iv in [7.5, 15, 25, 37.5, 50] for arm in ('on', 'off')]
    res = D + '/results.jsonl'; open(res, 'w').close()
    data = {}
    for rep in range(1, reps + 1):
        order = cells if rep % 2 else list(reversed(cells))
        for st, arm, iv in order:
            r = meas(st, arm, iv, rep)
            if r is None: continue
            open(res, 'a').write(json.dumps(r) + '\n')
            data.setdefault((st, iv, arm), {})[rep] = r['kbps'] if r['pass'] else None
        # rewrite summary each rep
        L = ['Clean GATT one-way FSU vs interval (matched off-arm, post-FSU window, Student-t) — %d rounds' % rep, '']
        for st in ('open', 'sdc'):
            L.append(f'[{st.upper()}]')
            L.append('  interval   off    on    FSU delta            n')
            for iv in [7.5, 15, 25, 37.5, 50]:
                on = data.get((st, iv, 'on'), {}); off = data.get((st, iv, 'off'), {})
                rounds = sorted(set(on) & set(off))
                deltas = [100 * (on[r] / off[r] - 1) for r in rounds if on.get(r) and off.get(r)]
                onv = [on[r] for r in rounds if on.get(r)]; offv = [off[r] for r in rounds if off.get(r)]
                if len(deltas) >= 2:
                    m = statistics.mean(deltas); sd = statistics.stdev(deltas); n = len(deltas)
                    ci = TCRIT.get(n, 2.0) * sd / math.sqrt(n)
                    L.append(f'  {iv:5}ms  {statistics.mean(offv):5.0f}  {statistics.mean(onv):5.0f}   {m:+5.1f}% ± {ci:4.1f}% (t)   {n}')
                else:
                    L.append(f'  {iv:5}ms  (insufficient accepted paired rounds: {len(deltas)})')
            L.append('')
        open(D + '/summary.txt', 'w').write('\n'.join(L))
    print('DONE')

if __name__ == '__main__':
    main()
