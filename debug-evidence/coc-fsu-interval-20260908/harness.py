#!/usr/bin/env python3
"""CoC one-way FSU vs connection interval — does the FSU gain reappear at longer intervals?

Resolves the open coc-open-fsu-20260813 caveat (+14.4% at 50ms/244B, but at a depressed 131 KB/s
baseline — interval lever, or low-RF-day artifact?). Measures CoC/open FSU on/off across
15/25/37.5/50 ms (480 B) + the exact 50 ms/244 B repro config, all same-RF, counterbalanced,
captured from boot (spacing=52 verified every measurement).

Central hexes prebuilt in /tmp/cocsweep/<dir>/zephyr/zephyr.hex; one shared coc-sink open-fsu.
"""
import sys, os, re, json, time, subprocess, statistics, math

REPO = '<REPO>'
D    = REPO + '/debug-evidence/coc-fsu-interval-20260908'
BLD  = '/tmp/cocsweep'
SINK = f'{BLD}/sink/zephyr/zephyr.hex'
CEN_ID, PER_ID = '1057794857', '1057719509'
CEN_TTY = '/dev/cu.usbmodem0010577948573'
PER_TTY = '/dev/cu.usbmodem0010577195093'
SECS = 30

# (config_label, interval_ms, sdu, fsu, central_build_dir)
CELLS = [
    ('15ms/480B', 15.0,  480, 'on',  'on12'),   ('15ms/480B', 15.0,  480, 'off', 'off12'),
    ('25ms/480B', 25.0,  480, 'on',  'on20'),   ('25ms/480B', 25.0,  480, 'off', 'off20'),
    ('37.5ms/480B', 37.5, 480, 'on',  'on30'),  ('37.5ms/480B', 37.5, 480, 'off', 'off30'),
    ('50ms/480B', 50.0,  480, 'on',  'on40'),   ('50ms/480B', 50.0,  480, 'off', 'off40'),
    ('50ms/244B', 50.0,  244, 'on',  'on40_244'),('50ms/244B', 50.0, 244, 'off', 'off40_244'),
]
RES = D + '/results.jsonl'
SUM = D + '/summary.txt'
CAPS = D + '/caps'


def program(hexf, dev):
    subprocess.run(['nrfutil', 'device', 'program', '--firmware', hexf, '--serial-number', dev], capture_output=True, timeout=120)

def reset(dev):
    subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', dev], capture_output=True, timeout=30)

def measure(cell, rnd):
    label, iv, sdu, fsu, cdir = cell
    cen = f'{BLD}/{cdir}/zephyr/zephyr.hex'
    tag = f'{label.replace("/","_")}_{fsu}_r{rnd}'
    program(SINK, PER_ID); program(cen, CEN_ID)
    os.makedirs(CAPS, exist_ok=True)
    env = dict(os.environ, CAP_OUTDIR=CAPS)
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS),
                          f'{CEN_TTY}:{tag}-cen', f'{PER_TTY}:{tag}-per'],
                         env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    reset(PER_ID); reset(CEN_ID)
    try: p.wait(timeout=SECS + 40)
    except Exception: p.kill()
    kbps, engaged = 0.0, False
    try:
        r = subprocess.run(['python3', REPO + '/tools/analyze.py', f'{CAPS}/{tag}-per.log'], capture_output=True, text=True, timeout=60)
        m = re.search(r'([0-9.]+)\s*KB/s', r.stdout, re.I)
        kbps = float(m.group(1)) if m else 0.0
    except Exception: pass
    for suf in ('-cen', '-per'):
        try:
            with open(f'{CAPS}/{tag}{suf}.log') as f:
                if re.search(r'spacing=52|fsu=52', f.read()): engaged = True; break
        except Exception: pass
    for suf in ('-cen', '-per'):   # bound disk: drop raw logs after extracting
        try: os.remove(f'{CAPS}/{tag}{suf}.log')
        except Exception: pass
    return kbps, engaged

def main():
    max_rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    recs, t0, rnd = [], time.time(), 0
    while rnd < max_rounds and time.time() - t0 < 4 * 3600:
        rnd += 1
        order = CELLS if rnd % 2 == 1 else list(reversed(CELLS))
        for cell in order:
            label, iv, sdu, fsu, cdir = cell
            kbps, engaged = measure(cell, rnd)
            rec = {'round': rnd, 'label': label, 'iv': iv, 'sdu': sdu, 'fsu': fsu, 'kbps': kbps, 'engaged': engaged}
            recs.append(rec)
            with open(RES, 'a') as f: f.write(json.dumps(rec) + '\n')
        # summary: FSU delta per config, paired per-round
        L = [f'CoC one-way FSU vs interval — {rnd} rounds, {SECS}s each', '']
        labels = ['15ms/480B', '25ms/480B', '37.5ms/480B', '50ms/480B', '50ms/244B']
        for lab in labels:
            on_all  = [r['kbps'] for r in recs if r['label'] == lab and r['fsu'] == 'on' and r['kbps'] > 0]
            off_all = [r['kbps'] for r in recs if r['label'] == lab and r['fsu'] == 'off' and r['kbps'] > 0]
            deltas = []
            for rr in range(1, rnd + 1):
                o = [r['kbps'] for r in recs if r['label'] == lab and r['fsu'] == 'on'  and r['round'] == rr and r['kbps'] > 0]
                f_ = [r['kbps'] for r in recs if r['label'] == lab and r['fsu'] == 'off' and r['round'] == rr and r['kbps'] > 0]
                if o and f_: deltas.append(100 * (o[0] / f_[0] - 1))
            mon = statistics.mean(on_all) if on_all else 0
            moff = statistics.mean(off_all) if off_all else 0
            eng = [r['engaged'] for r in recs if r['label'] == lab and r['fsu'] == 'on']
            er = f"{100*sum(eng)//max(len(eng),1)}% eng" if eng else ''
            if len(deltas) >= 2:
                d = statistics.mean(deltas); ci = 1.96 * statistics.stdev(deltas) / math.sqrt(len(deltas))
                L.append(f'  {lab:12}: off {moff:6.1f}  on {mon:6.1f}  FSU {d:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)}) {er}')
            elif deltas:
                L.append(f'  {lab:12}: off {moff:6.1f}  on {mon:6.1f}  FSU {deltas[0]:+5.1f}%  (n=1) {er}')
        with open(SUM, 'w') as f: f.write('\n'.join(L))
    print('DONE', rnd, 'rounds')

if __name__ == '__main__':
    main()
