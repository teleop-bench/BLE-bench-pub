#!/usr/bin/env python3
"""Multi-round counterbalanced FSU/parity campaign (prebuilt hexes, nRF54L15).

Goal: tight CIs on the FSU on/off deltas + open-vs-SDC parity, by measuring all 8
headline cells many times with alternating (counterbalanced) arm order so slow RF/thermal
drift cancels out of the paired per-round deltas.

Captures FROM BOOT (launch capture, then reset both boards into the open ports) so the
once-at-setup FSU line (spacing=52) is recorded and engagement is verified every measurement.

Modes:  smoke  = 1 round, then STOP (for inspection before the long run)
        full   = time-budgeted many-round campaign
"""
import sys, os, re, json, time, subprocess, statistics, math

REPO = '<REPO>'
D    = REPO + '/debug-evidence/fsu-campaign-20260907'
PRE  = REPO + '/prebuilt-hexes'
CEN_ID, PER_ID = '1057794857', '1057719509'
CEN_TTY = '/dev/cu.usbmodem0010577948573'
PER_TTY = '/dev/cu.usbmodem0010577195093'
SECS = 30

# (transport, stack, fsu, central_hex, sink_hex)
CELLS = [
    ('GATT', 'open', 'on',  'c-gatt-open-fsu-7p5', 'p-gatt-sink-open'),
    ('GATT', 'open', 'off', 'c-gatt-open-off-7p5', 'p-gatt-sink-open'),
    ('GATT', 'SDC',  'on',  'c-gatt-sdc-fsu-25',   'p-gatt-sink-sdc'),
    ('GATT', 'SDC',  'off', 'c-gatt-sdc-off-25',   'p-gatt-sink-sdc'),
    ('CoC',  'open', 'on',  'c-coc-open-fsu-15',   'p-coc-sink-open'),
    ('CoC',  'open', 'off', 'c-coc-open-off-15',   'p-coc-sink-open'),
    ('CoC',  'SDC',  'on',  'c-coc-sdc-fsu-15',    'p-coc-sink-sdc'),
    ('CoC',  'SDC',  'off', 'c-coc-sdc-off-15',    'p-coc-sink-sdc'),
]
RES = D + '/results.jsonl'
SUM = D + '/summary.txt'
CAPS = D + '/caps'


def program(hexf, dev):
    subprocess.run(['nrfutil', 'device', 'program', '--firmware', hexf, '--serial-number', dev],
                   capture_output=True, timeout=120)


def reset(dev):
    subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', dev], capture_output=True, timeout=30)


def measure(cell, rnd):
    tp, st, fsu, cen, sink = cell
    tag = f'{tp}_{st}_{fsu}_r{rnd}'
    program(f'{PRE}/{sink}.hex', PER_ID)
    program(f'{PRE}/{cen}.hex', CEN_ID)
    os.makedirs(CAPS, exist_ok=True)
    env = dict(os.environ, CAP_OUTDIR=CAPS)
    # capture from boot: launch capture, then reset both into the open ports
    p = subprocess.Popen(['python3', REPO + '/tools/capture-tool.py', str(SECS),
                          f'{CEN_TTY}:{tag}-cen', f'{PER_TTY}:{tag}-per'],
                         env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    reset(PER_ID); reset(CEN_ID)
    try:
        p.wait(timeout=SECS + 40)
    except Exception:
        p.kill()
    kbps, engaged = 0.0, False
    try:
        r = subprocess.run(['python3', REPO + '/tools/analyze.py', f'{CAPS}/{tag}-per.log'],
                           capture_output=True, text=True, timeout=60)
        m = re.search(r'([0-9.]+)\s*KB/s', r.stdout, re.I)
        kbps = float(m.group(1)) if m else 0.0
    except Exception:
        pass
    for suf in ('-cen', '-per'):
        try:
            with open(f'{CAPS}/{tag}{suf}.log') as f:
                if re.search(r'spacing=52|fsu=52', f.read()):
                    engaged = True; break
        except Exception:
            pass
    # keep only the on-cell cen logs as proof; delete the rest to bound disk over a long run
    if not (fsu == 'on'):
        for suf in ('-cen', '-per'):
            try: os.remove(f'{CAPS}/{tag}{suf}.log')
            except Exception: pass
    else:
        try: os.remove(f'{CAPS}/{tag}-per.log')
        except Exception: pass
    return kbps, engaged


def ci95(xs):
    xs = [x for x in xs if x > 0]
    n = len(xs)
    if n < 2:
        return (statistics.median(xs) if xs else 0.0, 0.0, n)
    sd = statistics.stdev(xs)
    sem = sd / math.sqrt(n)
    return (statistics.mean(xs), 1.96 * sem, n)


def summarize(recs, rnd, t0, done=False):
    def cell(tp, st, fsu):
        return [r['kbps'] for r in recs if r['tp'] == tp and r['st'] == st and r['fsu'] == fsu and r['kbps'] > 0]
    L = [f"FSU/parity campaign — {len([r for r in recs if r['kbps']>0])} good measurements, "
         f"{rnd} rounds, {SECS}s each, elapsed {int(time.time()-t0)}s" + ('  [DONE]' if done else ''), '']
    # per-cell absolute
    L.append('Per-cell throughput (mean ± 95% CI, KB/s):')
    for tp, st, fsu, *_ in CELLS:
        m, c, n = ci95(cell(tp, st, fsu))
        eng = [r['engaged'] for r in recs if r['tp'] == tp and r['st'] == st and r['fsu'] == fsu]
        er = f"{100*sum(eng)//max(len(eng),1)}% fsu-engaged" if eng else ''
        L.append(f'  {tp:4} {st:4} {fsu:3}: {m:6.1f} ± {c:4.1f}  (n={n})  {er}')
    # paired per-round FSU deltas (counterbalanced) with CI
    L.append('')
    L.append('FSU on/off delta — paired per-round (mean ± 95% CI):')
    for tp in ('GATT', 'CoC'):
        for st in ('open', 'SDC'):
            deltas = []
            for rr in range(1, rnd + 1):
                on = [r['kbps'] for r in recs if r['tp'] == tp and r['st'] == st and r['fsu'] == 'on' and r['round'] == rr and r['kbps'] > 0]
                off = [r['kbps'] for r in recs if r['tp'] == tp and r['st'] == st and r['fsu'] == 'off' and r['round'] == rr and r['kbps'] > 0]
                if on and off:
                    deltas.append(100 * (on[0] / off[0] - 1))
            if len(deltas) >= 2:
                mean = statistics.mean(deltas); ci = 1.96 * statistics.stdev(deltas) / math.sqrt(len(deltas))
                L.append(f'  FSU {tp:4} {st:4}: {mean:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)} paired rounds)')
            elif deltas:
                L.append(f'  FSU {tp:4} {st:4}: {deltas[0]:+5.1f}%  (n=1, CI pending)')
    # open-vs-SDC parity (fsu off), paired per-round
    L.append('')
    L.append('open-vs-SDC parity (FSU off) — paired per-round (mean ± 95% CI):')
    for tp in ('GATT', 'CoC'):
        deltas = []
        for rr in range(1, rnd + 1):
            o = [r['kbps'] for r in recs if r['tp'] == tp and r['st'] == 'open' and r['fsu'] == 'off' and r['round'] == rr and r['kbps'] > 0]
            s = [r['kbps'] for r in recs if r['tp'] == tp and r['st'] == 'SDC' and r['fsu'] == 'off' and r['round'] == rr and r['kbps'] > 0]
            if o and s:
                deltas.append(100 * (o[0] / s[0] - 1))
        if len(deltas) >= 2:
            mean = statistics.mean(deltas); ci = 1.96 * statistics.stdev(deltas) / math.sqrt(len(deltas))
            L.append(f'  open-vs-SDC {tp:4}: {mean:+5.1f}% ± {ci:4.1f}%  (n={len(deltas)})')
    return '\n'.join(L)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'smoke'
    time_budget = 0 if mode == 'smoke' else 10 * 3600
    max_rounds = 1 if mode == 'smoke' else 60
    recs = []
    t0 = time.time()
    rnd = 0
    while rnd < max_rounds and (mode == 'smoke' or time.time() - t0 < time_budget):
        rnd += 1
        order = CELLS if rnd % 2 == 1 else list(reversed(CELLS))  # counterbalance arm order
        for cell in order:
            tp, st, fsu, cen, sink = cell
            kbps, engaged = measure(cell, rnd)
            rec = {'round': rnd, 'tp': tp, 'st': st, 'fsu': fsu, 'kbps': kbps,
                   'engaged': engaged, 't': int(time.time() - t0)}
            recs.append(rec)
            with open(RES, 'a') as f:
                f.write(json.dumps(rec) + '\n')
        with open(SUM, 'w') as f:
            f.write(summarize(recs, rnd, t0))
    with open(SUM, 'w') as f:
        f.write(summarize(recs, rnd, t0, done=True))
    print('DONE', rnd, 'rounds')


if __name__ == '__main__':
    main()
