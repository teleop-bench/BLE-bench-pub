#!/usr/bin/env python3
"""Stage 3 — CoC-duplex-open FSU vs interval (HELD + STALL-gated). See METHODOLOGY.md in this dir.

Both directions measured (downlink = sink SINK-rx KB/s, uplink = central CENRX slope). EVERY cell gated
through verify_run.verify_duplex(): a wedge-STALLED rep (uplink~0) or a reverted rep is EXCLUDED and the
stall counted, so the curve reflects only healthy balanced duplex + an honest stall-rate. Auto-update-OFF
sink (FSU holds). CoC-duplex-open, v4.4.2, 7.5/15/25/37.5/50 ms.
"""
import os, sys, json, time, subprocess, statistics, math, hashlib, shutil
REPO = '<REPO>'
sys.path.insert(0, REPO + '/tools')
from verify_run import verify_duplex
D = REPO + '/debug-evidence/coc-duplex-fsu-interval-20260908'
CEN, PER = '1057794857', '1057719509'
CTTY = '/dev/cu.usbmodem0010577948573'; PTTY = '/dev/cu.usbmodem0010577195093'
SECS = 30
STACK = sys.argv[2] if len(sys.argv) > 2 else 'open'          # open | sdc
sfx = '' if STACK == 'open' else '-sdc'
CAPS = D + f'/caps{sfx}'; FW = D + f'/firmware{sfx}'
if STACK == 'open':
    SINK = '/tmp/dupsweep/sink/zephyr/zephyr.hex'             # coc-duplex-sink open-fsu, auto-update OFF
    cpath = lambda fsu, u: f'/tmp/dupsweep/{fsu}{u}/zephyr/zephyr.hex'
else:
    SINK = '/tmp/dupsweep-sdc/sink/coc-duplex-sink/zephyr/zephyr.hex'   # SDC, auto-update OFF (sysbuild-nested)
    cpath = lambda fsu, u: f'/tmp/dupsweep-sdc/{fsu}{u}/coc-duplex-central/zephyr/zephyr.hex'
U = {7.5: 6, 15: 12, 25: 20, 37.5: 30, 50: 40}
CELLS = []
for iv in [7.5, 15, 25, 37.5, 50]:
    CELLS.append((iv, 'on',  cpath('on', U[iv])))
    CELLS.append((iv, 'off', cpath('off', U[iv])))

def prog(h, d): subprocess.run(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', d], capture_output=True, timeout=120)
def rst(d): subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', d], capture_output=True, timeout=30)

def meas(cell, rep):
    iv, fsu, cen = cell
    tag = f'{iv}_{fsu}_r{rep}'.replace('.', 'p')
    if not os.path.exists(cen) or not os.path.exists(SINK):
        print(f'  {tag}: MISSING FIRMWARE', flush=True); return None
    prog(SINK, PER); prog(cen, CEN); os.makedirs(CAPS, exist_ok=True)
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS), f'{CTTY}:{tag}-cen', f'{PTTY}:{tag}-per'],
                         env=dict(os.environ, CAP_OUTDIR=CAPS), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    try: p.wait(timeout=SECS + 40)
    except Exception: p.kill()
    r = verify_duplex(f'{CAPS}/{tag}-per.log', f'{CAPS}/{tag}-cen.log', fsu=fsu, interval_ms=iv)
    print(f'  {tag}: DL {r["downlink"]:5.0f} UL {r["uplink"]:5.0f} agg {r["aggregate"]:5.0f}  '
          f'{"PASS" if r["pass"] else "REJECT "+str(r["rejects"])}', flush=True)
    return r

def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    os.makedirs(FW, exist_ok=True); seen = set()
    for _, _, cen in CELLS:
        for h in (cen, SINK):
            if h in seen or not os.path.exists(h): continue
            seen.add(h); shutil.copy(h, FW + '/' + h.replace('/tmp/', '').replace('/', '_'))
    with open(FW + '/SHA256SUMS', 'w') as f:
        for fn in sorted(os.listdir(FW)):
            if fn.endswith('.hex'): f.write(hashlib.sha256(open(FW + '/' + fn, 'rb').read()).hexdigest() + '  ' + fn + '\n')
    agg = {}; stalls = {}; RES = D + f'/results{sfx}.jsonl'
    for rep in range(1, reps + 1):
        for cell in (CELLS if rep % 2 else list(reversed(CELLS))):
            iv, fsu, _ = cell
            r = meas(cell, rep)
            if r is None: continue
            agg.setdefault((iv, fsu), []).append(r['aggregate'] if r['pass'] else None)
            stalls[iv] = stalls.get(iv, 0) + (0 if r['pass'] else 1)
            with open(RES, 'a') as f:
                f.write(json.dumps({'round': rep, 'iv': iv, 'fsu': fsu, 'downlink': r['downlink'],
                                    'uplink': r['uplink'], 'aggregate': r['aggregate'], 'accepted': r['pass'],
                                    'why': ';'.join(r['rejects']) if r['rejects'] else 'ok'}) + '\n')
        L = [f'CoC-duplex-open FSU vs interval (aggregate, stall-gated) — {rep} rounds', '',
             'interval   off-agg  on-agg   FSU(agg) delta      stalls']
        for iv in [7.5, 15, 25, 37.5, 50]:
            on = agg.get((iv, 'on'), []); off = agg.get((iv, 'off'), [])
            deltas = [100 * (on[i] / off[i] - 1) for i in range(min(len(on), len(off))) if on[i] and off[i] and off[i] > 0]
            onv = [x for x in on if x]; offv = [x for x in off if x]
            if len(deltas) >= 2:
                d = statistics.mean(deltas); ci = 1.96 * statistics.stdev(deltas) / math.sqrt(len(deltas))
                L.append(f'  {iv:5}ms   {statistics.mean(offv):6.0f}  {statistics.mean(onv):6.0f}   {d:+5.1f}% ± {ci:4.1f}% (n={len(deltas)})  stalls={stalls.get(iv,0)}')
            elif deltas:
                L.append(f'  {iv:5}ms   {statistics.mean(offv) if offv else 0:6.0f}  {statistics.mean(onv) if onv else 0:6.0f}   {deltas[0]:+5.1f}% (n=1)  stalls={stalls.get(iv,0)}')
            else:
                L.append(f'  {iv:5}ms   (no accepted paired rounds)  stalls={stalls.get(iv,0)}')
        open(D + f'/summary{sfx}.txt', 'w').write('\n'.join(L))
    print('DONE')

if __name__ == '__main__':
    main()
