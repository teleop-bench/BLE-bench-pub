#!/usr/bin/env python3
"""HELD, verify_run-GATED CoC+GATT one-way FSU vs connection interval (7.5/15/25/37.5/50 ms).

Supersedes the INVALIDATED coc-fsu-interval-20260908 (auto-update-revert). Here: auto-update-OFF sinks,
and EVERY measurement is passed through tools/verify_run.verify() — a reverted/misconfigured cell is
REJECTED (not counted), so the curve can't be contaminated by the revert bug again. Open controller,
v4.4.2, 480 B (CoC) / notify (GATT). Firmware+SHA persisted; raw logs kept.
"""
import os, sys, re, json, time, subprocess, statistics, math, hashlib, shutil
REPO = '<REPO>'
sys.path.insert(0, REPO + '/tools')
from verify_run import verify, parse_series
D = REPO + '/debug-evidence/fsu-interval-sweep-20260908'
CEN, PER = '1057794857', '1057719509'
CTTY = '/dev/cu.usbmodem0010577948573'; PTTY = '/dev/cu.usbmodem0010577195093'
SECS = 30; CAPS = D + '/caps'; FW = D + '/firmware'
SINK_COC = '/tmp/sink_noau/zephyr/zephyr.hex'                 # coc-sink, auto-update OFF
SINK_GATT = '/tmp/gatt442/sink/zephyr/zephyr.hex'             # z54-lat-periph, auto-update OFF
COC_U = {7.5: 6, 15: 12, 25: 20, 37.5: 30, 50: 40}
GATT_DIR = {7.5: ('on', 'off'), 15: ('on12', 'off12'), 25: ('on20', 'off20'),
            37.5: ('on30', 'off30'), 50: ('on40', 'off40')}
CELLS = []
for iv in [7.5, 15, 25, 37.5, 50]:
    u = COC_U[iv]
    CELLS.append(('CoC', iv, 'on',  f'/tmp/cocsweep/on{u}/zephyr/zephyr.hex',  SINK_COC))
    CELLS.append(('CoC', iv, 'off', f'/tmp/cocsweep/off{u}/zephyr/zephyr.hex', SINK_COC))
    gon, goff = GATT_DIR[iv]
    CELLS.append(('GATT', iv, 'on',  f'/tmp/gatt442/{gon}/zephyr/zephyr.hex',  SINK_GATT))
    CELLS.append(('GATT', iv, 'off', f'/tmp/gatt442/{goff}/zephyr/zephyr.hex', SINK_GATT))

def prog(h, d): subprocess.run(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', d], capture_output=True, timeout=120)
def rst(d): subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', d], capture_output=True, timeout=30)

def meas(cell, rep):
    tp, iv, fsu, cen, sink = cell
    tag = f'{tp}_{iv}_{fsu}_r{rep}'.replace('.', 'p')
    if not os.path.exists(cen) or not os.path.exists(sink):
        print(f'  {tag}: MISSING FIRMWARE', flush=True); return (0, False, 'missing-fw')
    prog(sink, PER); prog(cen, CEN); os.makedirs(CAPS, exist_ok=True)
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS),
                          f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                         env=dict(os.environ, CAP_OUTDIR=CAPS), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS + 40)
    except Exception: p.kill()
    per, cenl = f'{CAPS}/{tag}-per.log', f'{CAPS}/{tag}-cen.log'
    r = verify(per, cenl, fsu=fsu, interval_ms=iv)     # <-- the gate: rejects a reverted/misconfigured cell
    kbps = r.get('steady_kbps', 0.0)
    ok = r['pass']
    if not ok:
        print(f'  {tag}: steady {kbps:6.1f}  REJECT {r["rejects"]}', flush=True)
    else:
        print(f'  {tag}: steady {kbps:6.1f}  PASS held={r.get("fsu_held","n/a")}', flush=True)
    return (kbps, ok, ';'.join(r['rejects']) if r['rejects'] else 'ok')

def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    os.makedirs(FW, exist_ok=True)
    seen = set()
    for _, _, _, cen, sink in CELLS:
        for h in (cen, sink):
            if h in seen or not os.path.exists(h): continue
            seen.add(h); shutil.copy(h, FW + '/' + h.split('/tmp/')[1].replace('/', '_'))
    with open(FW + '/SHA256SUMS', 'w') as f:
        for fn in sorted(os.listdir(FW)):
            if fn.endswith('.hex'): f.write(hashlib.sha256(open(FW + '/' + fn, 'rb').read()).hexdigest() + '  ' + fn + '\n')
    data = {}   # (tp,iv,fsu) -> [kbps of PASSing measurements]
    RES = D + '/results.jsonl'
    for rep in range(1, reps + 1):
        for cell in (CELLS if rep % 2 else list(reversed(CELLS))):
            tp, iv, fsu, _, _ = cell
            kbps, ok, why = meas(cell, rep)
            data.setdefault((tp, iv, fsu), []).append(kbps if ok else None)   # None = rejected, excluded
            with open(RES, 'a') as f:
                f.write(json.dumps({'round': rep, 'tp': tp, 'iv': iv, 'fsu': fsu, 'kbps': kbps, 'accepted': ok, 'why': why}) + '\n')
        # curve so far: per (transport, interval) FSU delta from ACCEPTED paired rounds
        L = [f'FSU vs interval (HELD, verify_run-gated) — {rep} rounds, {SECS}s', '', 'transport interval  off    on     FSU delta        (n accepted)']
        for tp in ('CoC', 'GATT'):
            for iv in [7.5, 15, 25, 37.5, 50]:
                on = data.get((tp, iv, 'on'), []); off = data.get((tp, iv, 'off'), [])
                deltas = [100 * (on[i] / off[i] - 1) for i in range(min(len(on), len(off)))
                          if on[i] and off[i] and off[i] > 0]
                onv = [x for x in on if x]; offv = [x for x in off if x]
                if len(deltas) >= 2:
                    d = statistics.mean(deltas); ci = 1.96 * statistics.stdev(deltas) / math.sqrt(len(deltas))
                    L.append(f'  {tp:4} {iv:5}ms  {statistics.mean(offv):5.0f}  {statistics.mean(onv):5.0f}  {d:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)})')
                elif deltas:
                    L.append(f'  {tp:4} {iv:5}ms  {statistics.mean(offv) if offv else 0:5.0f}  {statistics.mean(onv) if onv else 0:5.0f}  {deltas[0]:+5.1f}%  (n=1)')
        rejected = sum(1 for v in data.values() for x in v if x is None)
        L.append(f'\n  rejected-by-gate (excluded): {rejected}')
        open(D + '/summary.txt', 'w').write('\n'.join(L))
    print('DONE')

if __name__ == '__main__':
    main()
