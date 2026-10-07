#!/usr/bin/env python3
"""v4.4.1 vs v4.4.2 CoC-open A/B — does the benchmark reproduce on the latest Zephyr release?

Same RF day, alternating arms so day-drift cancels: for each round, measure CoC one-way open
throughput at 15 ms for {FSU on, FSU off} on BOTH the v4.4.1 firmware (prebuilt-hexes/, our fork)
and the v4.4.2 firmware (fsu-m0 rebased onto v4.4.2, built here). Compares absolute KB/s and — the
transferable number — the FSU on/off delta, across many rounds for a stable mean + variance.

Firmware: v441 = prebuilt-hexes/c-coc-open-{fsu,off}-15 + p-coc-sink-open; v442 = firmware/
v442-coc-cen-{fsu,off} + v442-coc-sink (coc-central open-{fsu,nofsu}.conf @ CONN_INT_UNITS=12).
"""
import sys, os, re, json, time, subprocess, statistics

REPO = '<REPO>'
D = REPO + '/debug-evidence/v442-benchmark-20260907'
CEN_ID, PER_ID = '1057794857', '1057719509'
CEN_TTY = '/dev/cu.usbmodem0010577948573'
PER_TTY = '/dev/cu.usbmodem0010577195093'
SECS = 30
TIME_BUDGET_S = 12 * 3600
MAX_ROUNDS = 800
PRE, FW = REPO + '/prebuilt-hexes', D + '/firmware'
FW_SET = {
    'v441': {'fsu': (f'{PRE}/c-coc-open-fsu-15.hex', f'{PRE}/p-coc-sink-open.hex'),
             'off': (f'{PRE}/c-coc-open-off-15.hex', f'{PRE}/p-coc-sink-open.hex')},
    'v442': {'fsu': (f'{FW}/v442-coc-cen-fsu.hex', f'{FW}/v442-coc-sink.hex'),
             'off': (f'{FW}/v442-coc-cen-off.hex', f'{FW}/v442-coc-sink.hex')},
}
RES, SUM = D + '/results.jsonl', D + '/summary.txt'


def flash(hexf, dev):
    subprocess.run(['nrfutil', 'device', 'program', '--firmware', hexf, '--serial-number', dev],
                   capture_output=True, timeout=120)
    subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', dev], capture_output=True, timeout=30)


def measure(arm, cfg, rnd):
    cen, sink = FW_SET[arm][cfg]
    flash(sink, PER_ID); flash(cen, CEN_ID)
    subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', CEN_ID], capture_output=True)
    subprocess.run(['nrfutil', 'device', 'reset', '--serial-number', PER_ID], capture_output=True)
    tag = f'{arm}-{cfg}-r{rnd}'
    out = D + '/caps'; os.makedirs(out, exist_ok=True)
    env = dict(os.environ, CAP_OUTDIR=out)
    try:
        subprocess.run(['python3', REPO + '/tools/capture-tool.py', str(SECS),
                        f'{PER_TTY}:{tag}-per', f'{CEN_TTY}:{tag}-cen'],
                       env=env, capture_output=True, timeout=SECS + 40)
        r = subprocess.run(['python3', REPO + '/tools/analyze.py', f'{out}/{tag}-per.log'],
                           capture_output=True, text=True, timeout=60)
        m = re.search(r'([0-9.]+)\s*KB/s', r.stdout, re.I)
        return float(m.group(1)) if m else 0.0
    except Exception:
        return 0.0


def summarize(recs):
    def col(arm, cfg):
        return sorted(r['kbps'] for r in recs if r['arm'] == arm and r['cfg'] == cfg and r['kbps'] > 0)
    def med(xs):
        return round(statistics.median(xs), 1) if xs else 0
    L = [f'v4.4.1 vs v4.4.2 CoC-open A/B — {len(recs)} measurements ({SECS}s each)', '']
    for cfg in ('off', 'fsu'):
        a, b = col('v441', cfg), col('v442', cfg)
        L.append(f'  CoC open {cfg:3}: v4.4.1 med {med(a)} KB/s (n={len(a)})  |  v4.4.2 med {med(b)} KB/s (n={len(b)})'
                 + (f'  Δ={round(100*(med(b)-med(a))/med(a),1)}%' if med(a) else ''))
    # FSU delta each version
    for arm in ('v441', 'v442'):
        on, off = col(arm, 'fsu'), col(arm, 'off')
        if med(off):
            L.append(f'  FSU on/off delta {arm}: {round(100*(med(on)-med(off))/med(off),1)}%')
    return '\n'.join(L)


def main():
    recs = []
    t0 = time.time()
    rnd = 0
    while time.time() - t0 < TIME_BUDGET_S and rnd < MAX_ROUNDS:
        rnd += 1
        for arm in ('v441', 'v442'):          # alternate arms within a round -> drift cancels
            for cfg in ('off', 'fsu'):
                kbps = measure(arm, cfg, rnd)
                rec = {'round': rnd, 'arm': arm, 'cfg': cfg, 'kbps': kbps, 't': int(time.time() - t0)}
                recs.append(rec)
                with open(RES, 'a') as f:
                    f.write(json.dumps(rec) + '\n')
        with open(SUM, 'w') as f:
            f.write(summarize(recs) + f'\n  rounds={rnd} elapsed={int(time.time()-t0)}s\n')
    with open(SUM, 'w') as f:
        f.write(summarize(recs) + f'\n  DONE rounds={rnd} elapsed={int(time.time()-t0)}s\n')


if __name__ == '__main__':
    main()
