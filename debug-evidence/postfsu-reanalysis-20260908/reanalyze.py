#!/usr/bin/env python3
"""Derived analysis (READ-ONLY over archived evidence) — persists the corrections quoted in
docs/investigations/fsu-benchmark-technical-report.md that were previously ad-hoc:

  (A) CoC one-way FSU, recomputed over a POST-FSU window (last 12 s) + Student-t 95% CIs, from the raw
      caps of fsu-interval-sweep / sdc-fsu-interval-sweep (the summaries there use whole-run medians +
      1.96·SE — those stay untouched; this is the derived, corrected view the report §3 CoC rows cite).
  (B) Duplex CoC FSU (both stacks) recomputed with Student-t (the archived harness prints 1.96·SE; the
      repo rule forbids editing debug-evidence/**, so the corrected CI lives HERE, not in that harness).

Reads sibling debug-evidence dirs; writes only summary.txt in THIS dir. `python3 reanalyze.py`.
"""
import os, re, glob, statistics, math
DE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # debug-evidence/
TCRIT = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36, 9: 2.31, 10: 2.26}
IVTOK = {7.5: '7p5', 15: '15', 25: '25', 37.5: '37p5', 50: '50'}

def series(f):
    out = []
    for l in open(f):
        m = re.search(r'^\s*([0-9.]+)\s+P t=\d+s rxkBps=([0-9]+)', l) or re.search(r'^\s*([0-9.]+)\s+SINK rx:\s*([0-9]+)', l)
        if m: out.append((float(m.group(1)), int(m.group(2))))
    return out

def late_median(f, win=12.0):
    s = series(f)
    if not s: return None
    tmax = s[-1][0]; w = [v for t, v in s if t >= tmax - win and v > 0]
    return statistics.median(w) if len(w) >= 5 else None

def ci(deltas):
    n = len(deltas); sd = statistics.stdev(deltas)
    return TCRIT.get(n, 2.0) * sd / math.sqrt(n)

def load_accepted(resfile):
    """Set of (tp, iv, fsu, round) the ORIGINAL verifier accepted — the derived view must honor these
    (e.g. SDC/CoC 50 ms round 8 was rejected for an FSU step-down; including it would wrongly make n=8)."""
    import json
    acc = set()
    for l in open(resfile):
        r = json.loads(l)
        if r.get('accepted'): acc.add((r['tp'], r['iv'], r['fsu'], r['round']))
    return acc

def rewindow(capdir, accepted, label, transport, L, note=''):
    L.append(f'[{label}] {transport} one-way FSU — POST-FSU window (last 12 s), accepted-only, Student-t 95%{note}')
    for iv in [7.5, 15, 25, 37.5, 50]:
        tok = IVTOK[iv]
        def arm(a):
            d = {}
            for f in glob.glob(f'{capdir}/{transport}_{tok}_{a}_r*-per.log'):
                r = int(re.search(r'_r(\d+)-per', f).group(1))
                if (transport, iv, a, r) not in accepted: continue   # honor the archived verifier decision
                v = late_median(f)
                if v: d[r] = v
            return d
        on, off = arm('on'), arm('off')
        rounds = sorted(set(on) & set(off))
        deltas = [100 * (on[r] / off[r] - 1) for r in rounds]
        if len(deltas) >= 2:
            L.append(f'  {iv:5}ms  off {statistics.mean([off[r] for r in rounds]):5.0f}  on {statistics.mean([on[r] for r in rounds]):5.0f}   {statistics.mean(deltas):+5.1f}% ± {ci(deltas):4.1f}% (t, n={len(deltas)})')
        else:
            L.append(f'  {iv:5}ms  (insufficient paired reps: {len(deltas)})')
    L.append('')

def duplex_student_t(resfile, label, L):
    import json
    if not os.path.exists(resfile): return
    L.append(f'[{label}] CoC duplex FSU (aggregate, stall-gated) — Student-t 95% (accepted paired rounds only)')
    agg = {}
    for line in open(resfile):
        r = json.loads(line)
        if r.get('accepted'): agg.setdefault((r['iv'], r['fsu']), {})[r['round']] = r['aggregate']
    for iv in [7.5, 15, 25, 37.5, 50]:
        on, off = agg.get((iv, 'on'), {}), agg.get((iv, 'off'), {})
        rounds = sorted(set(on) & set(off))
        deltas = [100 * (on[r] / off[r] - 1) for r in rounds if off.get(r)]
        if len(deltas) >= 2:
            L.append(f'  {iv:5}ms  {statistics.mean(deltas):+5.1f}% ± {ci(deltas):4.1f}% (t, n={len(deltas)})')
        elif deltas:
            L.append(f'  {iv:5}ms  {deltas[0]:+5.1f}% (n=1 — no CI)')
        else:
            L.append(f'  {iv:5}ms  (no accepted paired rounds)')
    L.append('')

def main():
    L = ['Derived post-FSU / Student-t re-analysis of archived evidence (read-only, accepted-only). See README here', '']
    od = f'{DE}/fsu-interval-sweep-20260908'; sd = f'{DE}/sdc-fsu-interval-sweep-20260908'
    open_acc = load_accepted(f'{od}/results.jsonl'); sdc_acc = load_accepted(f'{sd}/results.jsonl')
    rewindow(f'{od}/caps', open_acc, 'OPEN', 'CoC', L)
    rewindow(f'{sd}/caps', sdc_acc, 'SDC', 'CoC', L)
    # OLD GATT logs re-windowed (single variable = window) — demonstrates the whole-run-median artifact.
    # These GATT numbers are SUPERSEDED by gatt-fsu-clean-20260908 (which also fixed the off-arm + sink);
    # shown only to confirm the old whole-run "+7% at 50 ms" SDC-GATT was a window artifact (→ ~+14%).
    # accepted-only too: e.g. the rejected old GATT/SDC 37.5 ms round 5 is excluded.
    rewindow(f'{od}/caps', open_acc, 'OPEN (OLD GATT logs, SUPERSEDED)', 'GATT', L, note=' — window artifact demo only')
    rewindow(f'{sd}/caps', sdc_acc, 'SDC (OLD GATT logs, SUPERSEDED)', 'GATT', L, note=' — window artifact demo only')
    duplex_student_t(f'{DE}/coc-duplex-fsu-interval-20260908/results.jsonl', 'open-duplex', L)
    duplex_student_t(f'{DE}/coc-duplex-fsu-interval-20260908/results-sdc.jsonl', 'sdc-duplex', L)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'summary.txt')
    open(out, 'w').write('\n'.join(L))
    print('\n'.join(L)); print(f'\nwrote {out}')

if __name__ == '__main__':
    main()
