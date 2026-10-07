"""Held-gate sensitivity: gain as reported (held gate on FSU-on only), with no held gate, and with the
gate applied to both arms. Re-measures from archived captures with the tools' own measure().

  python3 debug-evidence/replication-20261004/held-sensitivity.py      # all duplex cells, both sessions
"""
import importlib.util, json, os, re, statistics, sys, types, glob
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))

def load(name, both):
    src = open(os.path.join(REPO, 'tools', name)).read()
    if both:
        if 'gatt' in name:
            a = "        if arm == 'on' and not held: r['reject']"; assert src.count(a) == 1
            src = src.replace(a, "        if not held: r['reject']")
        else:
            a = "    if arm == 'on':\n        for name, ser in (('dl', dl), ('ul', ul)):"; assert src.count(a) == 1
            src = src.replace(a, "    if True:\n        for name, ser in (('dl', dl), ('ul', ul)):")
    m = types.ModuleType(name); m.__file__ = os.path.join(REPO, 'tools', name)
    exec(compile(src, name, 'exec'), m.__dict__); return m

def welch(a, b):
    ma, mb = statistics.mean(a), statistics.mean(b)
    if len(a) < 2 or len(b) < 2: return 100 * (mb / ma - 1), float('nan')
    va, vb = statistics.variance(a) / len(a), statistics.variance(b) / len(b)
    se = (va + vb) ** 0.5
    df = (va + vb) ** 2 / ((va ** 2 / (len(a) - 1)) + (vb ** 2 / (len(b) - 1))) if se else 1
    from math import isfinite
    t = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 12: 2.18, 14: 2.14}
    tc = t.get(max(1, min(14, int(df))), 2.1)
    return 100 * (mb / ma - 1), 100 * tc * se / ma

def run(tool, stack, rundir, label):
    res = [json.loads(l) for l in open(os.path.join(rundir, 'results.jsonl'))]
    caps = os.path.join(rundir, 'caps')
    out = {}
    for mode in ('reported', 'nohold', 'both'):
        m = load(tool, mode == 'both')
        rows = []
        for old in res:
            u = old.get('interval_units') or int(re.search(r'(\d+)_(on|off)', old['tag']).group(1))
            arm = old.get('arm') or re.search(r'_(on|off)_', old['tag']).group(1)
            if mode == 'reported':
                r = old
            else:
                r = m.measure(caps, old['tag'], arm, u, stack) if 'coc' in tool else m.measure(caps, old['tag'], arm, u * 1250)
                if mode == 'nohold':
                    r = dict(r); r['reject'] = [x for x in r['reject'] if 'not held' not in x]
            if r.get('agg') is None: continue
            rows.append((u, arm, r['agg'], not r['reject']))
        for u in sorted({x[0] for x in rows}):
            off = [a for uu, ar, a, ok in rows if uu == u and ar == 'off' and ok]
            on = [a for uu, ar, a, ok in rows if uu == u and ar == 'on' and ok]
            if off and on:
                g, ci = welch(off, on); out.setdefault(u, {})[mode] = f'{g:+5.1f}% ±{ci:4.1f} (n={len(off)}/{len(on)})'
    for u, d in out.items():
        print(f'{label:34s} {u*1.25:5.1f} ms | reported {d.get("reported","-"):24s} | no held gate {d.get("nohold","-"):24s} | gate both arms {d.get("both","-")}')

EV = os.path.join(REPO, 'debug-evidence')
RUNS = [
    ('gatt-duplex-fsu.py', 'open', 'gatt-duplex-fsu-model-20261003/open-15ms', '10-03 GATT open 15'),
    ('gatt-duplex-fsu.py', 'sdc', 'gatt-duplex-fsu-model-20261003/sdc-7p5-25ms', '10-03 GATT SDC 7.5/25'),
    ('gatt-duplex-fsu.py', 'sdc', 'gatt-duplex-fsu-model-20261003/sdc-15ms', '10-03 GATT SDC 15'),
    ('gatt-duplex-fsu.py', 'open', 'duplex-fsu-followups-20261003/model-test', '10-03 GATT open model'),
    ('gatt-duplex-fsu.py', 'open', 'duplex-fsu-followups-20261003/model-prereg', '10-03 GATT open prereg'),
    ('coc-duplex-fsu.py', 'open', 'coc-duplex-fsu-matched-20261003/open-full', '10-03 CoC open'),
    ('coc-duplex-fsu.py', 'sdc', 'coc-duplex-fsu-matched-20261003/sdc-full', '10-03 CoC SDC (7.5/15 used)'),
    ('coc-duplex-fsu.py', 'sdc', 'coc-duplex-fsu-matched-20261003/sdc-25-rerun', '10-03 CoC SDC 25'),
    ('coc-duplex-fsu.py', 'open', 'duplex-fsu-followups-20261003/coc-open25-n8', '10-03 CoC open 25 n8'),
    ('coc-duplex-fsu.py', 'sdc', 'duplex-fsu-followups-20261003/coc-sdc15-n8', '10-03 CoC SDC 15 n8'),
    ('gatt-duplex-fsu.py', 'open', 'replication-20261004/gatt-open', '10-04 GATT open'),
    ('gatt-duplex-fsu.py', 'sdc', 'replication-20261004/gatt-sdc', '10-04 GATT SDC'),
    ('coc-duplex-fsu.py', 'open', 'replication-20261004/coc-open', '10-04 CoC open'),
    ('coc-duplex-fsu.py', 'sdc', 'replication-20261004/coc-sdc', '10-04 CoC SDC'),
]

if __name__ == '__main__':
    for tool, stack, d, label in RUNS:
        d = os.path.join(EV, d)
        try: run(tool, stack, d, label)
        except Exception as e: print(label, 'ERROR', type(e).__name__, e)
