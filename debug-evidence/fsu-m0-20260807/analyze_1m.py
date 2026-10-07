#!/usr/bin/env python3
"""M0 1M spacing discriminator, analyzer rev 3 — registered while the
m0-1m43s series was IN FLIGHT and unread (supersedes rev 1/2, archived as
analyze_1m_rev2_superseded.py; stale 52-arm/ABAB language removed).

GEOMETRY: 1M PHY, interval 43 units (53.75 ms) via periph PREF auto-update,
DLE-251, blast sink, BT_BUF_ACL_TX_COUNT=33/EVT_RX=40 (central),
RX_EXTRA=20/CTLR_RX=18 (periph), BT_CTLR_ASSERT_DEBUG=n — NOTE: this
replaces the development assertion with the -ECANCELED fallthrough; runs
execute under sustained trx-busy cancellations (~14-18/s observed in
smokes), so arm-equivalence of cancellation rate is a REQUIRED gate below.

PRIMARY OBSERVABLE: per-cell throughput = median per-second periph rxkBps
(>10), first 25 kept samples dropped. Quantized model (robust for event
overhead 0..800 us): N150=21, N100=22 pairs/event -> 93.10 vs 97.53 KiB/s,
predicted delta +4.7619%.

SECONDARY OBSERVABLE (registered before series read; motivated by a
POST-HOC smoke finding, disclosed): within-run completion-gap step on
100-arms — median BLASTC cgap_min BEFORE the FSU-updated line vs AFTER.
Expected step -100 +/- 15 us (pair 2468 -> 2368). 150-arms: no step.

ACCEPTED-RUN CONFIRMATIONS (each reported per cell):
- 1M witness: 'BLAST: phy_update->2M rc=-5' present (2M attempt refused:
  2M compiled out) AND no 'PHY tx=2' anywhere in either log.
- 'interval now 43' present (the 53.75 ms update happened).
- DLE-251 on-air witness: median cgap_min in [2200, 2600] us — a
  27-byte-PDU pair would be ~500 us; MTU 247 alone does not prove DLE.
- 100-arm: 'FSU: updated status=0x00 spacing=100 us' (EXCLUDE if absent —
  the only exclusion rule). 150-arm: no FSU-updated line expected
  (no-change requests emit no event; any event = flag).
- Cancellation rate: median per-second '+N/s' cancels, per cell; GATE:
  arm-pooled medians within 30% of their mean, else the throughput claim
  is INCONCLUSIVE (cancellation dynamics unmatched), regardless of delta.
- Disconnects: disc delta WITHIN the accepted window (line-order aligned,
  after the 25th kept sample) == 0; markers: no FATAL / USAGE FAULT /
  Halting; at most one Booting per log.
- Baseline sanity: 150-arm median in [82, 95] KiB/s.

ANALYSIS: consecutive opposite-arm cells pair in run order (AB BA BA AB);
per-pair delta%; mean with 95% paired t-CI (n=4, t=3.182); order effect =
mean(AB) vs mean(BA). Interpretation (asymmetric): ~+4.8% with gates
green = strong indirect evidence negotiated spacing reaches on-air
pacing; flat = strong evidence against visible adoption on this path;
else investigate quantization/overhead/cancellation first. Qualified
on-air capture remains the formal physical gate.
"""
import re, os, glob, math

EV = os.path.dirname(os.path.abspath(__file__))
T95_3DF = 3.182

def series_lines(raw, pat):
    out = []
    for i, line in enumerate(raw.splitlines()):
        m = re.search(pat, line)
        if m:
            out.append((i, m))
    return out

def cell(tag):
    c = open(f'{EV}/{tag}-central.log', 'rb').read().decode('utf-8', 'replace')
    p = open(f'{EV}/{tag}-periph.log', 'rb').read().decode('utf-8', 'replace')
    flags = []
    rx = series_lines(p, r'rxkBps=(\d+)')
    kept = [(i, int(m.group(1))) for i, m in rx if int(m.group(1)) > 10]
    win = kept[25:]
    med = None
    if win:
        vals = sorted(v for _, v in win)
        med = vals[len(vals)//2]
    if 'BLAST: phy_update->2M rc=-5' not in c:
        # capture-head truncation loses the one-shot marker on late CDC
        # sync (fewer samples corroborate); distinguish from a real 2M link
        flags.append('no-1M-witness(head-truncation-suspect)' if len(kept) < 123
                     else 'no-1M-witness')
    if 'PHY tx=2' in c or 'PHY tx=2' in p: flags.append('2M-SWITCH')
    if 'interval now 43' not in c: flags.append('no-interval-43')
    fsu = re.search(r'FSU: updated status=0x00 spacing=(\d+) us', c)
    conf = int(fsu.group(1)) if fsu else None
    fsu_pos = c.find('FSU: updated')
    def cgap_mins(seg):
        g = sorted(int(m.group(1)) for m in
                   re.finditer(r'cgap_us\[min/avg/max\]=(\d+)/', seg)
                   if int(m.group(1)) > 1000)
        return g[len(g)//2] if g else None
    pre = cgap_mins(c[:fsu_pos]) if fsu_pos > 0 else None
    post = cgap_mins(c[fsu_pos:]) if fsu_pos > 0 else cgap_mins(c)
    dle_ref = post if post else pre
    if dle_ref is None or not (2200 <= dle_ref <= 2600): flags.append('no-DLE251-witness')
    # cancels over the SAME post-settle window as throughput (rev 3.2):
    # line-aligned to the central's data stream, first 25 lines dropped
    cl = series_lines(c, r'BLASTC comp=')
    canc_start = cl[25][0] if len(cl) > 25 else 0
    canc = sorted(int(m.group(1)) for i, m in
                  series_lines(c, r'cancels=\d+\(\+(\d+)/s\)') if i >= canc_start)
    canc_med = canc[len(canc)//2] if canc else None
    # Windowed disconnect check PER LOG (rev 3.1: concatenating the logs
    # mixed line-index spaces and swept in pre-reset residue at the capture
    # head - the periph logs disc=1 when the central's FLASH kills the link,
    # before the capture's own reset zeroes it).
    for raw, first_pat in ((c, r'BLASTC comp='), (p, r'rxkBps=[1-9]')):
        fl = series_lines(raw, first_pat)
        if not fl:
            continue
        start_line = fl[0][0] + 25
        wd = [int(m.group(1)) for i, m in series_lines(raw, r'disc=(\d+)')
              if i >= start_line]
        if len(wd) >= 2 and max(wd) > wd[0]:
            flags.append('POST-SETTLE-DISCONNECT')
            break
    for marker in ('FATAL', 'USAGE FAULT', 'Halting'):
        if marker in c or marker in p: flags.append(f'marker:{marker}')
    # Window-aware reboot classification (rev 3.2): boots BEFORE the first
    # data line are the flash-reset + capture-reset sequence (capture-head,
    # benign, counted); a boot AFTER data starts is a real in-window reboot.
    for raw, first_pat, who in ((c, r'BLASTC comp=', 'central'),
                                (p, r'rxkBps=[1-9]', 'periph')):
        fl = series_lines(raw, first_pat)
        boots = [i for i, m in series_lines(raw, r'Booting')]
        if not fl:
            continue
        inwin = [b for b in boots if b > fl[0][0]]
        if inwin:
            flags.append(f'IN-WINDOW-REBOOT:{who}')
        elif len(boots) > 1:
            flags.append(f'head-boots:{who}={len(boots)}')
    return dict(med=med, conf=conf, pre=pre, post=post, canc=canc_med,
                flags=flags, n=len(win))

def main():
    seq = []
    for t in sorted(glob.glob(f'{EV}/m0-1m43s-f*-central.log')):
        tag = os.path.basename(t)[:-12]
        m = re.match(r'm0-1m43s-f(\d+)-t(\d+)', tag)
        seq.append((int(m.group(2)), int(m.group(1)), tag))
    seq.sort()
    cells = []
    for ts, v, tag in seq:
        d = cell(tag)
        if v == 100 and d['conf'] != 100: d['flags'].append('EXCLUDE:no-spacing-confirm')
        if v == 150 and d['conf'] is not None: d['flags'].append(f'unexpected-evt-{d["conf"]}')
        if v == 150 and d['med'] is not None and not (82 <= d['med'] <= 95):
            d['flags'].append('BASELINE-OUT-OF-BAND')
        step = (d['post'] - d['pre']) if (d['pre'] and d['post']) else None
        print(f'{tag}: arm={v} rx_med={d["med"]} conf={d["conf"]} '
              f'cgap pre/post={d["pre"]}/{d["post"]} step={step} '
              f'cancels/s~{d["canc"]} n={d["n"]} {";".join(d["flags"])}')
        cells.append((v, d))
    cm = {}
    for arm in (150, 100):
        vals = sorted(d['canc'] for v, d in cells if v == arm and d['canc'] is not None)
        cm[arm] = vals[len(vals)//2] if vals else None
    if cm[150] is not None and cm[100] is not None:
        canc_ok = abs(cm[150] - cm[100]) <= 0.3 * (cm[150] + cm[100]) / 2
        print(f'cancel-rate medians: 150-arm {cm[150]}/s, 100-arm {cm[100]}/s -> '
              f'{"MATCHED" if canc_ok else "UNMATCHED (throughput claim INCONCLUSIVE)"}')
    deltas, orders = [], []
    i = 0
    while i + 1 < len(cells):
        (v1, d1), (v2, d2) = cells[i], cells[i+1]
        if {v1, v2} == {150, 100} and d1['med'] and d2['med'] and \
           not any('EXCLUDE' in f for f in d1['flags'] + d2['flags']):
            base = d1['med'] if v1 == 150 else d2['med']
            test = d2['med'] if v1 == 150 else d1['med']
            deltas.append((test - base) / base * 100)
            orders.append('AB' if v1 == 150 else 'BA')
            print(f'pair {len(deltas)} [{orders[-1]}]: {base} -> {test}  delta={deltas[-1]:+.2f}%')
            i += 2
        else:
            i += 1
    if len(deltas) >= 2:
        n = len(deltas); mean = sum(deltas)/n
        sd = math.sqrt(sum((d-mean)**2 for d in deltas)/(n-1))
        half = T95_3DF * sd / math.sqrt(n)
        print(f'mean paired delta {mean:+.2f}%  95% CI [{mean-half:+.2f}, {mean+half:+.2f}] '
              f'(n={n}; model +4.76%; t=3.182 exact only for n=4)')
        ab = [d for d, o in zip(deltas, orders) if o == 'AB']
        ba = [d for d, o in zip(deltas, orders) if o == 'BA']
        if ab and ba:
            print(f'order effect: AB mean {sum(ab)/len(ab):+.2f}% vs BA mean {sum(ba)/len(ba):+.2f}%')
    steps = [(d['post'] - d['pre']) for v, d in cells if v == 100 and d['pre'] and d['post']]
    if steps:
        print(f'cgap steps (100-arms): {steps} — registered expectation -100 +/- 15 us each')
        ok = all(-115 <= s <= -85 for s in steps)
        print(f'SECONDARY (cgap step): {"PASS" if ok else "FAIL"}')

if __name__ == '__main__':
    main()
