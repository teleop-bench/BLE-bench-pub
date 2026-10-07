#!/usr/bin/env python3
"""Reset-recovery endurance — raise the reconnect n from 50 (endurance-1m §14.7) to ~100.

Drives N forced resets (alternating peripheral / central) on the coclat2 two-channel rig — the same
dedicated-lane architecture the safety doc recommends — and confirms the link fully re-establishes
after each (both L2CAP channels back up = a fresh "CTRL chan up" in the central log). Reports
recovered/N and recovery-time distribution. A reset that doesn't yield a new CTRL-up within the
timeout is a WEDGE (recorded, not hidden).

Runs the central serial capture for the whole run and polls it. `python3 harness.py [N] [gap_s]`.
"""
import os, sys, re, json, time, subprocess
REPO = '<REPO>'
D = REPO + '/debug-evidence/reset-recovery-100-20260909'
CEN, PER = '1057794857', '1057719509'
CTTY = '/dev/cu.usbmodem0010577948573'; PTTY = '/dev/cu.usbmodem0010577195093'
CEN_HEX = '/tmp/coclat2/cen/zephyr/zephyr.hex'; SINK_HEX = '/tmp/coclat2/sink/zephyr/zephyr.hex'
N = int(sys.argv[1]) if len(sys.argv) > 1 else 100
GAP = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
RECOVERY_TIMEOUT = 20.0
CAPS = D + '/caps'; CEN_LOG = CAPS + '/reset-cen.log'

def prog(h, d): return subprocess.run(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', d], capture_output=True, timeout=120).returncode == 0
def rst(d): subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', d], capture_output=True, timeout=30)

def ctrl_up_count():
    try: return sum(1 for l in open(CEN_LOG, errors='ignore') if 'CTRL chan up' in l)
    except Exception: return 0

def main():
    os.makedirs(CAPS, exist_ok=True)
    if not (os.path.exists(CEN_HEX) and os.path.exists(SINK_HEX)):
        print('MISSING FW — build coclat2 first'); return 1
    print(f'programming both boards (coclat2)…', flush=True)
    prog(SINK_HEX, PER); prog(CEN_HEX, CEN)
    dur = int(N * (GAP + RECOVERY_TIMEOUT) + 120)
    cap = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(dur), f'{CTTY}:reset-cen', f'{PTTY}:reset-per'],
                           env=dict(os.environ, CAP_OUTDIR=CAPS), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); rst(PER); rst(CEN)
    # wait for the initial connection
    t0 = time.time()
    while ctrl_up_count() < 1 and time.time() - t0 < 30: time.sleep(0.5)
    if ctrl_up_count() < 1: print('initial connect FAILED'); cap.kill(); return 1
    print(f'connected; driving {N} resets (alt PER/CEN, {GAP}s gap)…', flush=True)
    res = D + '/results.jsonl'; open(res, 'w').close()
    events = []
    for i in range(1, N + 1):
        target, serial = ('PER', PER) if i % 2 else ('CEN', CEN)
        pre = ctrl_up_count(); t_reset = time.time()
        rst(serial)
        recovered = False; rec_s = None
        while time.time() - t_reset < RECOVERY_TIMEOUT:
            if ctrl_up_count() > pre:
                recovered = True; rec_s = round(time.time() - t_reset, 1); break
            time.sleep(0.3)
        e = {'i': i, 'target': target, 'recovered': recovered, 'recovery_s': rec_s}
        events.append(e); open(res, 'a').write(json.dumps(e) + '\n')
        print(f'  {i:3}/{N} reset {target}: {"recovered in "+str(rec_s)+"s" if recovered else "*** WEDGE (no CTRL-up in %gs) ***" % RECOVERY_TIMEOUT}', flush=True)
        time.sleep(GAP)
    cap.terminate()
    rec = [e for e in events if e['recovered']]; times = [e['recovery_s'] for e in rec]
    wedges = [e for e in events if not e['recovered']]
    import statistics
    L = ['Reset-recovery endurance (coclat2 dedicated-lane rig) — forced-reset reconnect', '',
         f'recovered {len(rec)}/{N}  (PER resets {sum(1 for e in rec if e["target"]=="PER")}, CEN resets {sum(1 for e in rec if e["target"]=="CEN")})']
    if times:
        L.append(f'recovery time: min {min(times)}s  mean {statistics.mean(times):.1f}s  max {max(times)}s')
    if wedges:
        L.append(f'WEDGES ({len(wedges)}): ' + ', '.join(f'#{e["i"]}({e["target"]})' for e in wedges))
    else:
        L.append('WEDGES: 0 — every reset recovered first-try')
    open(D + '/summary.txt', 'w').write('\n'.join(L))
    print('\n' + '\n'.join(L)); print('DONE')

if __name__ == '__main__':
    main()
