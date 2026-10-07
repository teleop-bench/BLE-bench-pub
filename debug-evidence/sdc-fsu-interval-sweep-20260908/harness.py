#!/usr/bin/env python3
"""HELD, verify_run-gated SDC (SoftDevice) one-way FSU vs interval — the SDC half of the sweep.

Matches the open sweep (fsu-interval-sweep-20260908): auto-update-OFF SDC sinks, matched intervals
(7.5/15/25/37.5/50 ms), every cell gated through verify_run.verify(..., stack='sdc') (SDC uses 65/70us
frame space, no 52 token; held = no throughput step-down + positive delta). NCS v3.4.0 sysbuild firmware.
"""
import os, sys, json, time, subprocess, statistics, math, hashlib, shutil, glob
REPO = '<REPO>'
sys.path.insert(0, REPO + '/tools')
from verify_run import verify
D = REPO + '/debug-evidence/sdc-fsu-interval-sweep-20260908'
CEN, PER = '1057794857', '1057719509'
CTTY = '/dev/cu.usbmodem0010577948573'; PTTY = '/dev/cu.usbmodem0010577195093'
SECS = 30; CAPS = D + '/caps'; FW = D + '/firmware'
SINK_COC = '/tmp/sdc/coc_sink/coc-sink/zephyr/zephyr.hex'          # SDC CoC sink, auto-update OFF
SINK_GATT = '/tmp/sdc/gatt_sink/z54-lat-periph/zephyr/zephyr.hex'  # SDC GATT sink, auto-update OFF

def coc(fsu, iv):
    if iv == 15: return f'/tmp/sdc/coc_{fsu}/coc-central/zephyr/zephyr.hex'
    u = {7.5: 6, 25: 20, 37.5: 30, 50: 40}[iv]
    return f'/tmp/sdcsweep/coc_{fsu}_{u}/coc-central/zephyr/zephyr.hex'
def gatt(fsu, iv):
    if iv == 7.5: return f'/tmp/sdc/gatt_{fsu}/z54-lat-central/zephyr/zephyr.hex'
    u = {15: 12, 25: 20, 37.5: 30, 50: 40}[iv]
    return f'/tmp/sdcsweep/gatt_{fsu}_{u}/z54-lat-central/zephyr/zephyr.hex'
CELLS = []
for iv in [7.5, 15, 25, 37.5, 50]:
    CELLS.append(('CoC', iv, 'on',  coc('on', iv),  SINK_COC))
    CELLS.append(('CoC', iv, 'off', coc('off', iv), SINK_COC))
    CELLS.append(('GATT', iv, 'on',  gatt('on', iv),  SINK_GATT))
    CELLS.append(('GATT', iv, 'off', gatt('off', iv), SINK_GATT))

def prog(h, d): subprocess.run(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', d], capture_output=True, timeout=120)
def rst(d): subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', d], capture_output=True, timeout=30)

def meas(cell, rep):
    tp, iv, fsu, cen, sink = cell
    tag = f'{tp}_{iv}_{fsu}_r{rep}'.replace('.', 'p')
    if not os.path.exists(cen) or not os.path.exists(sink):
        print(f'  {tag}: MISSING FIRMWARE ({cen if not os.path.exists(cen) else sink})', flush=True); return (0, False, 'missing-fw')
    prog(sink, PER); prog(cen, CEN); os.makedirs(CAPS, exist_ok=True)
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS), f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                         env=dict(os.environ, CAP_OUTDIR=CAPS), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS + 40)
    except Exception: p.kill()
    r = verify(f'{CAPS}/{tag}-per.log', f'{CAPS}/{tag}-cen.log', fsu=fsu, interval_ms=iv, stack='sdc')
    kbps = r.get('steady_kbps', 0.0); ok = r['pass']
    print(f'  {tag}: steady {kbps:6.1f}  {"PASS" if ok else "REJECT "+str(r["rejects"])}', flush=True)
    return (kbps, ok, ';'.join(r['rejects']) if r['rejects'] else 'ok')

def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    os.makedirs(FW, exist_ok=True); seen = set()
    for _, _, _, cen, sink in CELLS:
        for h in (cen, sink):
            if h in seen or not os.path.exists(h): continue
            seen.add(h); shutil.copy(h, FW + '/' + h.replace('/tmp/', '').replace('/', '_'))
    with open(FW + '/SHA256SUMS', 'w') as f:
        for fn in sorted(os.listdir(FW)):
            if fn.endswith('.hex'): f.write(hashlib.sha256(open(FW + '/' + fn, 'rb').read()).hexdigest() + '  ' + fn + '\n')
    data = {}; RES = D + '/results.jsonl'
    for rep in range(1, reps + 1):
        for cell in (CELLS if rep % 2 else list(reversed(CELLS))):
            tp, iv, fsu, _, _ = cell
            kbps, ok, why = meas(cell, rep)
            data.setdefault((tp, iv, fsu), []).append(kbps if ok else None)
            with open(RES, 'a') as f:
                f.write(json.dumps({'round': rep, 'tp': tp, 'iv': iv, 'fsu': fsu, 'kbps': kbps, 'accepted': ok, 'why': why}) + '\n')
        L = [f'SDC FSU vs interval (HELD, verify_run-gated) — {rep} rounds', '', 'transport interval  off    on     FSU delta        (n)']
        for tp in ('CoC', 'GATT'):
            for iv in [7.5, 15, 25, 37.5, 50]:
                on = data.get((tp, iv, 'on'), []); off = data.get((tp, iv, 'off'), [])
                deltas = [100 * (on[i] / off[i] - 1) for i in range(min(len(on), len(off))) if on[i] and off[i] and off[i] > 0]
                onv = [x for x in on if x]; offv = [x for x in off if x]
                if len(deltas) >= 2:
                    d = statistics.mean(deltas); ci = 1.96 * statistics.stdev(deltas) / math.sqrt(len(deltas))
                    L.append(f'  {tp:4} {iv:5}ms  {statistics.mean(offv):5.0f}  {statistics.mean(onv):5.0f}  {d:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)})')
                elif deltas:
                    L.append(f'  {tp:4} {iv:5}ms  {statistics.mean(offv) if offv else 0:5.0f}  {statistics.mean(onv) if onv else 0:5.0f}  {deltas[0]:+5.1f}%  (n=1)')
        rej = sum(1 for v in data.values() for x in v if x is None)
        L.append(f'\n  rejected-by-gate: {rej}')
        open(D + '/summary.txt', 'w').write('\n'.join(L))
    print('DONE')

if __name__ == '__main__':
    main()
