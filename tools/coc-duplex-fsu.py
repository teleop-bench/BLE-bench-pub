#!/usr/bin/env python3
"""L2CAP CoC duplex FSU on/off — independent traffic each way, matched arms, held FSU, ABBA.

coc-duplex-central blasts on its CoC channel while coc-duplex-sink blasts back on the same channel:
two independent streams (not an echo). aggregate = sink `SINK rx` (downlink A->B) + central
`CENRX cum_total` slope (uplink B->A), medians/slope over [FSU completion (FSU-off: the request)
+ 2 s, end of capture]. See REPRODUCE.md, "CoC duplex FSU (matched arms)".

Reconnect wedge: the first CoC since the CENTRAL booted can stall its uplink at any tested interval (7.5-25 ms). So after
flashing each central arm the first connection is discarded, and every measured rep is a fresh
connection made by resetting ONLY the peripheral (central stays booted). The capture is opened
first, so the reconnection and its FSU lines are recorded. A rep is rejected if the central
rebooted during it, and a stalled direction is counted as a stall, never averaged in.

  python3 tools/coc-duplex-fsu.py --stack open --out <dir> [--smoke] [--intervals 6,12,20]
  python3 tools/coc-duplex-fsu.py --stack sdc  --out <dir> --ncs-root <NCS v3.4.0 workspace>
  options: --builds <dir>  --skip-build  --cen ID:TTY --per ID:TTY
  --cold   reset BOTH boards each rep (every rep is a first-since-boot connection), for an even split.
           NOT usable for measurements: with these images the reconnect wedge stalled 11 of 16 cold
           reps at 7.5/15 ms (duplex-fsu-followups-20261003). Kept to reproduce that finding.
  --abba N ABBA blocks per interval (default 2 -> n=4 per arm; 4 -> n=8).
"""
import argparse, json, math, os, re, statistics, subprocess, sys, time

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from verify_run import fsu_held  # noqa: E402
import link_gates  # noqa: E402

B = 'nrf54l15dk/nrf54l15/cpuapp'
ORDER = ['off', 'on', 'on', 'off', 'off', 'on', 'on', 'off']
SECS = 40
CFG = {
    'open': dict(on='open-fsu.conf', off='open-nofsu.conf', sink='open-fsu.conf',
                 sink_checks=['CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US=52', 'CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y']),
    'sdc': dict(on='sdc-sel.conf;sdc-fsu.conf', off='sdc-sel.conf;sdc-nofsu.conf', sink='sdc-sel.conf;sdc.conf',
                sink_checks=['CONFIG_BT_CTLR_SDC_ENABLE_LOWEST_FRAME_SPACE=y', 'CONFIG_BT_LL_SOFTDEVICE=y']),
}
TCRIT = {2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306}


def sh(cmd, timeout=900, cwd=None, env=None):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd, env=env)


def die(msg):
    sys.exit(f'ERROR: {msg}')


def build(stack, app, bdir, conf, extra, ncs_root):
    args = ['west', 'build', '-p', 'always', '-b', B, os.path.join(REPO, app), '-d', bdir, '--',
            f'-DEXTRA_CONF_FILE={conf}'] + extra
    if stack == 'open':
        r = sh(args)
    else:
        env = dict(os.environ); env.pop('ZEPHYR_BASE', None)
        r = sh(['nrfutil', 'toolchain-manager', 'launch', '--ncs-version', 'v3.4.0', '--'] + args, cwd=ncs_root, env=env)
    if r.returncode:
        die(f'build failed: {bdir}\n{r.stdout[-1500:]}{r.stderr[-800:]}')


def img(stack, bdir, app):
    return os.path.join(bdir, os.path.basename(app), 'zephyr') if stack == 'sdc' else os.path.join(bdir, 'zephyr')


def detect():
    out = sh(['nrfutil', 'device', 'list'], timeout=60).stdout
    boards = []
    for blk in re.split(r'\n\s*\n', out):
        lines = blk.strip().splitlines()
        if not lines or not re.match(r'\s*\d{6,}\s*$', lines[0]) or 'PCA10156' not in blk:
            continue
        for ln in lines:
            m = re.search(r'(/dev/tty\S+), vcom: 1', ln)
            if m:
                boards.append((lines[0].strip(), m.group(1).replace('/dev/tty.', '/dev/cu.'))); break
    if len(boards) != 2:
        die(f'need exactly 2 nRF54L15-DK (PCA10156), found {len(boards)}; pass --cen ID:TTY --per ID:TTY')
    return boards[0], boards[1]


def ts(l):
    m = re.match(r'\s*([0-9.]+)\s', l)
    return float(m.group(1)) if m else None


def measure(caps, tag, arm, units, stack, cold=False):
    r = {'tag': tag, 'arm': arm, 'interval_units': units, 'stack': stack, 'reject': [], 'stall': None}
    try:
        cen_all = open(f'{caps}/{tag}-cen.log', errors='ignore').read().splitlines()
        per_all = open(f'{caps}/{tag}-per.log', errors='ignore').read().splitlines()
    except FileNotFoundError as e:   # the capture tool could not open a serial port (flaky USB)
        r['reject'].append(f'capture file missing ({os.path.basename(e.filename)})'); return r
    if not cold and any('Booting Zephyr' in l for l in cen_all):
        r['reject'].append('central rebooted during the rep (cold connection)')
    conn = [i for i, l in enumerate(cen_all) if 'GAP connected' in l]
    if not conn:
        r['reject'].append('no reconnection seen'); return r
    cen = cen_all[conn[-1]:]
    t0 = ts(cen[0]) or 0.0
    per = [l for l in per_all if (ts(l) or 0) >= t0 - 0.5]
    done = [(ts(l), int(re.search(r'spacing=(\d+)', l).group(1))) for l in cen if 'Q3FSU-DONE role=C' in l]
    req = [(ts(l), l) for l in cen if 'Q3FSU-REQ role=C' in l]
    if arm == 'on':
        if not done:
            r['reject'].append('no Q3FSU-DONE role=C'); return r
        onset, spacing = done[-1]
        if spacing >= 150: r['reject'].append(f'spacing={spacing} not reduced')
        if stack == 'open':
            pd = [int(re.search(r'spacing=(\d+)', l).group(1)) for l in per if 'Q3FSU-DONE role=P' in l]
            if not pd or pd[-1] >= 150: r['reject'].append(f'peripheral spacing {pd[-1:] or "missing"} not reduced')
    else:
        if any(sp < 150 for _, sp in done): r['reject'].append('off-arm reached a reduced spacing')
        okreq = [t for t, l in req if 'min=150 max=150' in l and 'rc=0' in l]
        if not okreq:
            r['reject'].append('no Q3FSU-REQ min=150 max=150 rc=0'); return r
        onset, spacing = (done[-1] if done else (okreq[-1], 150))
    r['onset_s'] = onset; r['spacing'] = spacing
    gates = {int(m.group(1)) for l in cen for m in [re.search(r'GATE conn: interval=(\d+)', l)] if m}
    r['interval_seen'] = sorted(gates)
    if gates != {units}: r['reject'].append(f'interval units {sorted(gates)} != {units}')
    r['reject'] += link_gates.phy_dle(cen) + link_gates.credit_window(cen, per)
    w0 = onset + 2.0
    dl = [(ts(l), int(m.group(1))) for l in per for m in [re.search(r'SINK rx: (\d+) KB/s', l)] if m and (ts(l) or -1) >= w0]
    cum = [(ts(l), int(m.group(1))) for l in cen for m in [re.search(r'CENRX cum_total=(\d+) B', l)] if m and (ts(l) or -1) >= w0]
    if len(dl) < 15 or len(cum) < 15:
        r['reject'].append(f'short window dl={len(dl)} ul={len(cum)}'); return r
    ul = [(cum[i][0], (cum[i][1] - cum[i-1][1]) / 1024.0 / max(1e-3, cum[i][0] - cum[i-1][0])) for i in range(1, len(cum))]
    # Downlink from the sink's cumulative byte counter (like the uplink): the per-second "SINK rx: N KB/s" line is
    # integer-truncated and its period is ~1.013 s (k_msleep(1000) + a blocking printk), so its median reads ~1% high.
    # The per-second series is kept only for the FSU-held shape check.
    dcum = [(ts(l), int(m.group(1))) for l in per for m in [re.search(r'SINK rx:.*cum_total=(\d+) B', l)] if m and (ts(l) or -1) >= w0]
    r['dl_line_median'] = statistics.median(k for _, k in dl)
    r['dl'] = ((dcum[-1][1] - dcum[0][1]) / 1024.0 / max(1e-3, dcum[-1][0] - dcum[0][0])) if len(dcum) >= 15 else r['dl_line_median']
    r['ul'] = (cum[-1][1] - cum[0][1]) / 1024.0 / max(1e-3, cum[-1][0] - cum[0][0])
    r['agg'] = round(r['dl'] + r['ul'], 1); r['dl'] = round(r['dl'], 1); r['ul'] = round(r['ul'], 1)
    if r['dl'] < 5 or r['ul'] < 5:
        r['stall'] = 'downlink' if r['dl'] < 5 else 'uplink'
        r['reject'].append(f"{r['stall']} stalled (dl={r['dl']} ul={r['ul']})")
        return r
    if arm == 'on':
        for name, ser in (('dl', dl), ('ul', ul)):
            held, why, det = fsu_held(ser, settle=w0)
            if not held: r['reject'].append(f'{name} not held: {why} {det}')
    return r


def summary(results, intervals):
    out = []
    for u in intervals:
        rs = [r for r in results if r['interval_units'] == u]
        on = [r['agg'] for r in rs if r['arm'] == 'on' and not r['reject']]
        off = [r['agg'] for r in rs if r['arm'] == 'off' and not r['reject']]
        stalls = sum(1 for r in rs if r['stall']); rej = sum(1 for r in rs if r['reject'])
        head = f'{u*1.25:>5} ms'
        if len(on) < 2 or len(off) < 2:
            out.append(f'{head}  on={on} off={off}  rejected={rej} (stalls={stalls})  too few reps for a delta'); continue
        mo, mn = statistics.mean(off), statistics.mean(on)
        v1, v2 = statistics.variance(on) / len(on), statistics.variance(off) / len(off)
        df = (v1 + v2) ** 2 / (v1 ** 2 / (len(on) - 1) + v2 ** 2 / (len(off) - 1)) if v1 + v2 > 0 else len(on) + len(off) - 2
        ci = TCRIT.get(max(2, min(8, int(df))), 2.306) * math.sqrt(v1 + v2)
        sep = 'strict non-overlap' if min(on) > max(off) or max(on) < min(off) else 'ranges overlap'
        dls = {a: [r['dl'] for r in rs if r['arm'] == a and not r['reject']] for a in ('off', 'on')}
        uls = {a: [r['ul'] for r in rs if r['arm'] == a and not r['reject']] for a in ('off', 'on')}
        out.append(f'{head}  off {mo:6.1f} (n={len(off)})  on {mn:6.1f} (n={len(on)})  FSU {100*(mn-mo)/mo:+.1f}% ± '
                   f'{100*ci/mo:.1f}% (Welch t95)  {sep}  rejected={rej} (stalls={stalls})')
        out.append(f'          per direction (dl/ul): off {statistics.mean(dls["off"]):.1f}/{statistics.mean(uls["off"]):.1f}'
                   f'  on {statistics.mean(dls["on"]):.1f}/{statistics.mean(uls["on"]):.1f}')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--stack', choices=['open', 'sdc'], required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--intervals', default='6,12,20')
    ap.add_argument('--builds'); ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--smoke', action='store_true'); ap.add_argument('--cen'); ap.add_argument('--per')
    ap.add_argument('--cold', action='store_true')
    ap.add_argument('--abba', type=int, default=2, help='ABBA blocks per interval (2 -> n=4 per arm, 4 -> n=8)')
    ap.add_argument('--ncs-root', default=os.environ.get('NCS_ROOT'))
    ap.add_argument('--cen-extra', default='', help='extra -D flags for both central arms (space-separated)')
    ap.add_argument('--sink-extra', default='', help='extra -D flags for the sink, e.g. a shallower controller '
                    'TX queue: "-DCONFIG_BT_BUF_ACL_TX_COUNT=20 -DCONFIG_BT_ATT_TX_COUNT=64"')
    a = ap.parse_args()
    c = CFG[a.stack]; intervals = [int(x) for x in a.intervals.split(',')]
    builds = a.builds or os.path.join(REPO, 'build', f'coc-duplex-{a.stack}')
    if a.stack == 'sdc' and not a.skip_build and not a.ncs_root:
        die('--ncs-root (or NCS_ROOT) is required for SDC builds')
    cen_app, sink_app = 'apps/coc/coc-duplex-central', 'apps/coc/coc-duplex-sink'
    if not a.skip_build:
        print(f'== building {a.stack} into {builds}', flush=True)
        for u in intervals:
            for arm in ('on', 'off'):
                build(a.stack, cen_app, f'{builds}/c-{arm}-{u}', c[arm], [f'-DCONFIG_APP_CONN_INT_UNITS={u}'] + a.cen_extra.split(), a.ncs_root)
        build(a.stack, sink_app, f'{builds}/sink', c['sink'], ['-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n'] + a.sink_extra.split(), a.ncs_root)
    print('== design gate', flush=True)
    for u in intervals:
        r = sh([sys.executable, os.path.join(REPO, 'tools', 'check_matched_pair.py'),
                os.path.join(img(a.stack, f'{builds}/c-on-{u}', cen_app), '.config'),
                os.path.join(img(a.stack, f'{builds}/c-off-{u}', cen_app), '.config'),
                '--allow', 'CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US'], timeout=60)
        print(f'  {u}: ' + (r.stdout.strip().splitlines() or ['?'])[-1], flush=True)
        if r.returncode: die('central arms are not a matched pair')
    scfg = open(os.path.join(img(a.stack, f'{builds}/sink', sink_app), '.config')).read()
    for want in c['sink_checks'] + ['# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set']:
        if want not in scfg: die(f'sink config lacks: {want}')
    print('  sink: FSU-capable, auto-update off', flush=True)
    if a.cen and a.per:
        (cen, ctty), (per, ptty) = a.cen.split(':', 1), a.per.split(':', 1)
    else:
        (cen, ctty), (per, ptty) = detect()
    print(f'  central {cen} {ctty} / peripheral {per} {ptty}', flush=True)

    def flash(h, sn):
        # `nrfutil device program` leaves the core halted: reset so the new image actually runs.
        r = sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
        if r.returncode: die(f'flash failed {h}: {r.stderr[-300:]}')
        sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)

    caps = os.path.join(a.out, 'caps'); os.makedirs(caps, exist_ok=True)
    flash(os.path.join(img(a.stack, f'{builds}/sink', sink_app), 'zephyr.hex'), per)
    order = ['off', 'on'] if a.smoke else ['off', 'on', 'on', 'off'] * a.abba
    results = []
    for u in intervals:
        loaded = None
        for i, arm in enumerate(order):
            if arm != loaded:            # new central image: boot it and discard the first (cold) connection
                flash(os.path.join(img(a.stack, f'{builds}/c-{arm}-{u}', cen_app), 'zephyr.hex'), cen)
                if not a.cold: time.sleep(15)
                loaded = arm
            tag = f'coc{a.stack}{u}_{arm}_r{i+1}'
            p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(SECS),
                                  f'{ctty}:{tag}-cen', f'{ptty}:{tag}-per'],
                                 env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            sh(['nrfutil', 'device', 'reset', '--serial-number', per], timeout=60)   # PERIPHERAL ONLY
            if a.cold:
                sh(['nrfutil', 'device', 'reset', '--serial-number', cen], timeout=60)
            p.wait(timeout=SECS + 60)
            r = measure(caps, tag, arm, u, a.stack, a.cold); r['cold'] = a.cold; results.append(r)
            print(json.dumps({k: r.get(k) for k in ('tag', 'spacing', 'onset_s', 'dl', 'ul', 'agg', 'stall', 'reject') if k in r}), flush=True)
            with open(os.path.join(a.out, 'results.jsonl'), 'a') as f: f.write(json.dumps(r) + '\n')
    print('\n=== SUMMARY (accepted reps only) ===')
    for line in summary(results, intervals): print(line)
    sys.exit(1 if any(r['reject'] for r in results) else 0)


if __name__ == '__main__':
    main()
