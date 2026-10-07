#!/usr/bin/env python3
"""Duplex campaign analysis — implements the ANALYSIS CONTRACT in
duplex-plan.md (registered 2026-08-06 ~21:50, commit 77c9fd2, BEFORE any
dx-* results were read). Estimators, warm-up, symmetry-as-measured,
5%-imbalance flag with no exclusions, paired t-CI (n pairs, t table),
per-run confirmations, incomplete-run annotation — all per contract.
KiB/s throughout (divisor 1024).
"""
import re, os, sys, csv, math

EV = os.path.dirname(os.path.abspath(__file__))
T95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447}  # df = n-1

def series(path, pat):
    """Per-second KiB/s samples and cumulative disc counters, each tagged with
    their raw-log line index. disc= is NOT always on the throughput-bearing
    line (the central's counter lives on its own status lines), so the two
    streams are parsed independently and aligned by line order."""
    vals, disc = [], []
    try:
        raw = open(path, 'rb').read().decode('utf-8', 'replace')
    except FileNotFoundError:
        return None, None, ''
    for i, line in enumerate(raw.splitlines()):
        m = re.search(pat, line)
        if m:
            vals.append((i, int(m.group(1))))
        d = re.search(r'disc=(\d+)', line)
        if d:
            disc.append((i, int(d.group(1))))
    return vals, disc, raw

def run_estimate(vals):
    """Contract: keep values >10, drop first 15 kept samples, mean of rest."""
    if vals is None:
        return None, 0
    kept = [v for _, v in vals if v > 10]
    rest = kept[15:]
    if not rest:
        return None, 0
    return sum(rest) / len(rest), len(rest)

def confirmations(raw_c, raw_p, need_rate=None, need_fsu=False, is_75=False):
    """Patterns pinned to the exact log lines (verified across all arms)."""
    flags = []
    if 'PHY tx=2 rx=2' not in raw_c:
        flags.append('no-PHY-2/2-confirm')
    if 'mtu=247' not in raw_c:
        flags.append('MTU!=247')
    if need_rate and f'rate_changed status=0x00 interval={need_rate} us' not in raw_c:
        flags.append(f'no-rate-{need_rate}-confirm')
    if is_75 and 'GAP connected interval=7500' not in raw_c:
        flags.append('no-7500-confirm')  # flag-only, beyond-contract check
    if need_fsu and 'status=0x00 spacing=70 us' not in raw_c:
        flags.append('EXCLUDE:no-spacing-confirm')
    return flags

def disc_increment(disc, vals, settle=15):
    """Post-settle disconnect: the board's cumulative disc counter increments
    during the accepted window (line-order aligned; pre-history excluded).
    Returns None (UNVERIFIABLE) when the log has no disc counter stream."""
    if not disc:
        return None
    kept_lines = [ln for ln, v in vals if v > 10] if vals else []
    if len(kept_lines) <= settle:
        return False
    start_line = kept_lines[settle]
    window = [d for ln, d in disc if ln >= start_line]
    return len(window) >= 2 and max(window) > window[0]

def analyze_run(tag, need_rate=None, need_fsu=False, is_75=False):
    cpath = f'{EV}/{tag}-central.log'
    ppath = f'{EV}/{tag}-periph.log'
    fwd_vals, fwd_disc, raw_p = series(ppath, r'rxkBps=(\d+)')
    rev_vals, rev_disc, raw_c = series(cpath, r'exkBps=(\d+)')
    fwd, nf = run_estimate(fwd_vals)
    rev, nr = run_estimate(rev_vals)
    flags = confirmations(raw_c, raw_p, need_rate, need_fsu, is_75)
    di_p = disc_increment(fwd_disc, fwd_vals)
    di_c = disc_increment(rev_disc, rev_vals)
    if di_p or di_c:
        flags.append('POST-SETTLE-DISCONNECT')  # included with annotation
    if di_p is None or di_c is None:
        flags.append('disc-unverifiable-' + ('periph' if di_p is None else 'central'))
    row = {'run': tag, 'fwd_KiBps': fwd, 'rev_KiBps': rev,
           'n_fwd': nf, 'n_rev': nr, 'aggregate': None,
           'imbalance_pct': None, 'flags': ''}
    if fwd is None or rev is None:
        flags.append('MISSING-WINDOW' if (fwd is None and rev is None) else 'PARTIAL')
    else:
        row['aggregate'] = fwd + rev
        m = (fwd + rev) / 2
        imb = abs(fwd - rev) / m * 100 if m else 0
        row['imbalance_pct'] = round(imb, 2)
        if imb > 5:
            flags.append('ASYMMETRIC')  # reported AND included, per contract
        if min(nf, nr) < 100:
            flags.append('SHORT<100s')  # included with annotation
    row['flags'] = ';'.join(flags)
    return row

def cell_summary(rows):
    ags = [r['aggregate'] for r in rows if r['aggregate'] is not None]
    if not ags:
        return None
    return sum(ags) / len(ags), min(ags), max(ags), len(ags)

def main():
    out = []
    print('== Cell D-open75 (open Zephyr, 7.5 ms) ==')
    for i in range(1, 6):
        out.append(analyze_run(f'dx-open75-run{i}', is_75=True))
    print('== Cell D-sdc75 (SDC, 7.5 ms) ==')
    for i in range(1, 6):
        out.append(analyze_run(f'dx-sdc75-run{i}', is_75=True))
    pairs = []
    order = 'AB BA BA AB AB BA'.split()
    for pn, pair in enumerate(order, 1):
        pr = {}
        for arm in pair:
            need_fsu = (arm == 'B')
            r = analyze_run(f'dxpair{pn}-{arm}', need_rate='50000', need_fsu=need_fsu)
            out.append(r)
            pr[arm] = r
        pairs.append((pn, pr))

    for r in out:
        print(f"{r['run']:16s} fwd={r['fwd_KiBps'] and round(r['fwd_KiBps'],1)} "
              f"rev={r['rev_KiBps'] and round(r['rev_KiBps'],1)} "
              f"agg={r['aggregate'] and round(r['aggregate'],1)} "
              f"imb%={r['imbalance_pct']} n=({r['n_fwd']},{r['n_rev']}) {r['flags']}")

    for name, sl in (('D-open75', out[0:5]), ('D-sdc75', out[5:10])):
        s = cell_summary(sl)
        if s:
            print(f'{name}: mean-agg={s[0]:.1f} KiB/s range [{s[1]:.1f},{s[2]:.1f}] n={s[3]}')

    # FSU paired analysis: per-pair % delta on aggregate (and per-direction)
    deltas, dfw, drv = [], [], []
    for pn, pr in pairs:
        a, b = pr.get('A'), pr.get('B')
        if not a or not b or a['aggregate'] is None or b['aggregate'] is None:
            print(f'pair{pn}: missing arm — dropped from CI')
            continue
        if 'EXCLUDE' in b['flags']:
            print(f'pair{pn}: FSU arm excluded (no spacing confirm)')
            continue
        d = (b['aggregate'] - a['aggregate']) / a['aggregate'] * 100
        deltas.append(d)
        dfw.append((b['fwd_KiBps'] - a['fwd_KiBps']) / a['fwd_KiBps'] * 100)
        drv.append((b['rev_KiBps'] - a['rev_KiBps']) / a['rev_KiBps'] * 100)
        print(f'pair{pn} ({order[pn-1]}): agg {a["aggregate"]:.1f} -> {b["aggregate"]:.1f}  '
              f'delta={d:+.2f}% (fwd {dfw[-1]:+.2f}%, rev {drv[-1]:+.2f}%)')
    n = len(deltas)
    if n >= 2:
        mean = sum(deltas) / n
        sd = math.sqrt(sum((d - mean) ** 2 for d in deltas) / (n - 1))
        half = T95[n - 1] * sd / math.sqrt(n)
        print(f'FSU duplex delta: {mean:+.2f}% 95% CI [{mean-half:+.2f}, {mean+half:+.2f}] '
              f'(n={n} pairs, t={T95[n-1]}, within-session, one board pair)')
        print(f'per-direction means: fwd {sum(dfw)/n:+.2f}%, rev {sum(drv)/n:+.2f}%')
    else:
        print(f'FSU duplex delta: insufficient pairs (n={n}) for CI')

    with open(f'{EV}/duplex.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(out[0].keys()))
        w.writeheader()
        w.writerows(out)
    print(f'duplex.csv written, {len(out)} rows')

def selftest_two_pairs():
    deltas = [10.0, 12.0]
    n = len(deltas); mean = sum(deltas)/n
    sd = math.sqrt(sum((d-mean)**2 for d in deltas)/(n-1))
    half = T95[n-1]*sd/math.sqrt(n)
    assert abs(T95[1] - 12.706) < 1e-9
    assert abs(half - 12.706*1.0) < 1e-9, half
    print(f'selftest n=2: mean {mean:+.2f}% CI [{mean-half:+.2f}, {mean+half:+.2f}] OK')

if __name__ == '__main__':
    if '--selftest' in sys.argv:
        selftest_two_pairs()
    else:
        main()
