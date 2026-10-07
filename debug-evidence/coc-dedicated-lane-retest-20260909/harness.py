#!/usr/bin/env python3
"""Dedicated-lane (two-channel CoC) re-test — n reps of the §11.3 priority-lane claim.

The original §11.3 result (~33 ms, min 19 / max 69, single session) reported the control-channel
stop-signal RTT under a saturating bulk blast on a SEPARATE CoC channel. But its own log shows the
control channel DEGRADING over time (pings 20→0, timeouts appearing). This re-run measures the
SUSTAINED-saturation window (t≥10 s), n reps, reset-isolated, and reports the honest distribution:
mean/max RTT, %-over-30 ms (the safety budget), and whether the control channel STARVES.

Rig: coclat2-central (bulk PSM 0x0080 + control ping-pong PSM 0x0081) + coclat2-sink, open-fsu,
pinned 15 ms, FSU spacing=52. `python3 harness.py <reps>`.
"""
import os, sys, re, json, time, subprocess, statistics, math
REPO = '<REPO>'
D = REPO + '/debug-evidence/coc-dedicated-lane-retest-20260909'
CEN, PER = '1057794857', '1057719509'
CTTY = '/dev/cu.usbmodem0010577948573'; PTTY = '/dev/cu.usbmodem0010577195093'
CEN_HEX = '/tmp/coclat2/cen/zephyr/zephyr.hex'; SINK_HEX = '/tmp/coclat2/sink/zephyr/zephyr.hex'
SECS = 36
TCRIT = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36, 9: 2.31, 10: 2.26}
LAT2 = re.compile(r'^\s*([0-9.]+)\s+LAT2: pings=(\d+) rtt_us\[min/mean/max\]=(\d+)/(\d+)/(\d+) over30ms=(\d+) to=(\d+) \| bulk=(\d+)\(\+(\d+)\)')

def prog(h, d):
    return subprocess.run(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', d], capture_output=True, timeout=120).returncode == 0
def rst(d): subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', d], capture_output=True, timeout=30)

def window_stats(rows, lo, hi):
    """rows = list of (t, pings, min, mean, max, over30, to, bulk, dbulk). Aggregate over t in [lo,hi]."""
    w = [r for r in rows if lo <= r[0] <= hi]
    pings = sum(r[1] for r in w); over30 = sum(r[5] for r in w); to = sum(r[6] for r in w)
    if pings == 0:
        return {'pings': 0, 'over30': over30, 'to': to, 'mean_ms': None, 'max_ms': None, 'pct_over30': None}
    mean_ms = sum(r[3] * r[1] for r in w) / pings / 1000.0
    max_ms = max((r[4] for r in w), default=0) / 1000.0
    dbulk = sum(r[8] for r in w)
    return {'pings': pings, 'over30': over30, 'to': to, 'mean_ms': round(mean_ms, 1),
            'max_ms': round(max_ms, 1), 'pct_over30': round(100 * over30 / pings, 1), 'dbulk': dbulk}

def meas(rep):
    tag = f'r{rep}'; caps = D + '/caps'; os.makedirs(caps, exist_ok=True)
    if not (os.path.exists(CEN_HEX) and os.path.exists(SINK_HEX)):
        print(f'  {tag}: MISSING FW', flush=True); return None
    if not (prog(SINK_HEX, PER) and prog(CEN_HEX, CEN)):
        print(f'  {tag}: FLASH FAILED', flush=True); return None
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS), f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                         env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS + 40)
    except Exception: p.kill()
    cen_log = f'{caps}/{tag}-cen.log'; per_log = f'{caps}/{tag}-per.log'
    rows = []
    for l in open(cen_log, errors='ignore'):
        m = LAT2.match(l)
        if m: rows.append((float(m.group(1)),) + tuple(int(m.group(i)) for i in range(2, 10)))
    # gate: FSU floor held on the responder (sink prints fsu=52). Fall back to the central log,
    # which also prints fsu=52 on the LAT2 line, if the sink log lacks it.
    def has_fsu52(path):
        try: return bool(re.search(r'fsu=52', open(path, errors='ignore').read()))
        except Exception: return False
    fsu_ok = has_fsu52(per_log) or has_fsu52(cen_log)
    early = window_stats(rows, 2, 6)      # the "good first window" the original likely quoted
    steady = window_stats(rows, 10, SECS) # the honest sustained-saturation window
    saturated = steady.get('dbulk', 0) > 200   # bulk kept flowing through the steady window
    starved = steady['pings'] == 0
    ok = fsu_ok and saturated and not starved
    r = {'rep': rep, 'fsu_ok': fsu_ok, 'saturated': saturated, 'starved': starved,
         'early': early, 'steady': steady, 'pass': ok}
    s = steady
    print(f'  {tag}: steady mean={s["mean_ms"]}ms max={s["max_ms"]}ms over30={s["pct_over30"]}% '
          f'pings={s["pings"]} to={s["to"]}  {"PASS" if ok else "REJECT"}'
          f'{"" if fsu_ok else " [fsu!=52]"}{"" if not starved else " [CTRL STARVED]"}{"" if saturated else " [not saturated]"}', flush=True)
    return r

def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    res = D + '/results.jsonl'; open(res, 'w').close()
    accepted = []
    for rep in range(1, reps + 1):
        r = meas(rep)
        if r is None: continue
        open(res, 'a').write(json.dumps(r) + '\n')
        if r['pass']: accepted.append(r)
    L = ['Dedicated-lane (two-channel CoC) re-test — sustained-saturation control RTT (t>=10s)', '',
         f'accepted {len(accepted)}/{reps} reps', '']
    if len(accepted) >= 2:
        means = [a['steady']['mean_ms'] for a in accepted]
        pcts = [a['steady']['pct_over30'] for a in accepted]
        maxes = [a['steady']['max_ms'] for a in accepted]
        n = len(accepted); t = TCRIT.get(n, 2.0)
        L.append(f'  control RTT mean:  {statistics.mean(means):.1f} ms ± {t*statistics.stdev(means)/math.sqrt(n):.1f} (t, n={n})')
        L.append(f'  % over 30 ms:      {statistics.mean(pcts):.1f}% ± {t*statistics.stdev(pcts)/math.sqrt(n):.1f} (t, n={n})')
        L.append(f'  worst max RTT:     {max(maxes):.1f} ms (across accepted reps)')
        L.append(f'  budget verdict:    {"HOLDS <=30ms" if statistics.mean(pcts) < 5 else "MARGINAL/BUSTS budget"} under sustained saturation')
    starved = sum(1 for a in [json.loads(l) for l in open(res)] if a.get('starved'))
    L.append(f'  control-channel STARVED (pings=0 steady): {starved}/{reps} reps')
    open(D + '/summary.txt', 'w').write('\n'.join(L))
    print('\n' + '\n'.join(L)); print('DONE')

if __name__ == '__main__':
    main()
