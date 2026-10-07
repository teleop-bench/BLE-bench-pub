#!/usr/bin/env python3
"""GATT echo duplex FSU on/off, matched arms, held-verified, post-onset window, ABBA.

Builds (optional), design-gates, flashes and measures the aggregate duplex throughput of a
z54-lat-central (write-without-response blast) + z54-lat-periph (echo as notify) pair with FSU on
vs off, where the two central arms differ ONLY in the requested frame space. See REPRODUCE.md,
"GATT duplex FSU (matched arms)". Needs 2x nRF54L15-DK, nrfutil, python3 + pyserial; open builds
need ZEPHYR_BASE (patched fsu-m0 tree) + west; SDC builds need an NCS v3.4.0 workspace (--ncs-root).

  python3 tools/gatt-duplex-fsu.py --stack open --out <dir>            # build + ABBA at 7.5 and 25 ms
  python3 tools/gatt-duplex-fsu.py --stack sdc  --out <dir> --smoke    # one rep per arm first
  python3 tools/gatt-duplex-fsu.py --stack open --out <dir> --diag     # + exchanges per connection event
  options: --intervals 6,20  --builds <dir>  --skip-build  --cen ID:TTY --per ID:TTY  --ncs-root <dir>

--diag (open stack only) builds the centrals with CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y, which makes
z54-lat-central print the controller's transactions-per-connection-event histogram ('RPT pe:'); the
mode/mean over the post-onset window is reported per rep. Diag builds go to a separate build dir;
quote throughput from non-diag runs.

aggregate = central `exkBps` (echo received, B->A) + peripheral `rxkBps` (blast received, A->B),
medians over [FSU completion (FSU-off: the request) + 2 s, end of capture]. The echo couples the two
directions, so only the aggregate is a result; the per-direction split is not a symmetry measure.
"""
import argparse, json, math, os, re, statistics, subprocess, sys, time

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from verify_run import fsu_held  # noqa: E402
import link_gates  # noqa: E402

B = 'nrf54l15dk/nrf54l15/cpuapp'
ORDER = ['off', 'on', 'on', 'off', 'off', 'on', 'on', 'off']        # ABBA x2 -> n=4 per arm
CFG = {
    'open': dict(cen=['tput-open.conf', 'fsu-open.conf'], on='hh-fsu52.conf', off='hh-fsu150.conf',
                 per='tput-echo-open.conf;fsu-open.conf', secs=45,
                 per_checks=['CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US=52', 'CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y']),
    'sdc': dict(cen=['sdc-sel.conf', 'tput.conf'], on='hh-sdc-fsu.conf', off='hh-sdc-nofsu.conf',
                per='sdc-sel.conf;tput-echo.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf', secs=60,
                per_checks=['CONFIG_BT_CTLR_SDC_ENABLE_LOWEST_FRAME_SPACE=y', 'CONFIG_BT_LL_SOFTDEVICE=y']),
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


def app_dir(stack, bdir, app):        # SDC builds are sysbuild: the app image is nested
    return os.path.join(bdir, os.path.basename(app)) if stack == 'sdc' else bdir


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


def after_last_boot(path):
    lines = open(path, errors='ignore').read().splitlines()
    idx = max([i for i, l in enumerate(lines) if 'Booting Zephyr' in l] or [0])
    return lines[idx:]


def ts(l):
    m = re.match(r'\s*([0-9.]+)\s', l)
    return float(m.group(1)) if m else None


def measure(caps, tag, arm, iv_us):
    cen = after_last_boot(f'{caps}/{tag}-cen.log'); per = after_last_boot(f'{caps}/{tag}-per.log')
    r = {'tag': tag, 'arm': arm, 'interval_us': iv_us, 'reject': []}
    r['reject'] += link_gates.phy_dle(cen) + link_gates.interval_held(cen, round(iv_us / 1250))
    upd = [(ts(l), int(re.search(r'spacing=(\d+)', l).group(1))) for l in cen if 'FSU: updated' in l and 'spacing=' in l]
    req = [(ts(l), l) for l in cen if 'FSU: request' in l]
    if arm == 'on':                    # open grants 52 us, SDC 65/70 us: require < 150
        if not upd:
            r['reject'].append('no FSU completion line'); return r
        onset, spacing = upd[-1]
        if spacing >= 150: r['reject'].append(f'spacing={spacing} not reduced')
    else:                              # a 150 request on a 150 link is a no-op: no 'updated' event
        if any(sp < 150 for _, sp in upd): r['reject'].append('off-arm reached a reduced spacing')
        okreq = [t for t, l in req if '[150..150]' in l and 'rc=0' in l]
        if not okreq:
            r['reject'].append('no FSU request [150..150] rc=0 line'); return r
        onset, spacing = (upd[-1] if upd else (okreq[-1], 150))
    r['onset_s'] = onset; r['spacing'] = spacing
    ivs = {int(m.group(1)) for l in cen for m in [re.search(r'interval=(\d+)', l)] if m}
    r['intervals_seen'] = sorted(ivs)
    if ivs != {iv_us}: r['reject'].append(f'interval {sorted(ivs)} != {iv_us}')
    discs = {m.group(1) for l in per for m in [re.search(r'disc=(\d+)', l)] if m}
    if discs - {'0'}: r['reject'].append(f'disconnects {sorted(discs)}')
    ex = [(ts(l), int(m.group(1))) for l in cen for m in [re.search(r'exkBps=(\d+)', l)] if m]
    rx = [(ts(l), int(m.group(1))) for l in per for m in [re.search(r'rxkBps=(\d+)', l)] if m]
    w0 = onset + 2.0
    exw = [k for t, k in ex if t >= w0]; rxw = [k for t, k in rx if t >= w0]
    if len(exw) < 15 or len(rxw) < 15:
        r['reject'].append(f'short window ex={len(exw)} rx={len(rxw)}'); return r
    r['ex_med'] = statistics.median(exw); r['rx_med'] = statistics.median(rxw); r['agg'] = r['ex_med'] + r['rx_med']
    hist = {}
    for l in cen:
        if 'RPT pe:' in l and ts(l) is not None and ts(l) >= w0:
            for k, n in re.findall(r' (\d+):(\d+)', l.split('|', 1)[1]):
                hist[int(k)] = hist.get(int(k), 0) + int(n)
    if hist:
        tot = sum(hist.values())
        r['pe_mode'] = max(hist, key=hist.get)
        r['pe_mean'] = round(sum(k * n for k, n in hist.items()) / tot, 2)
        r['pe_mode_share'] = round(hist[r['pe_mode']] / tot, 3)
    for name, ser in (('ex', ex), ('rx', rx)):
        held, why, det = fsu_held([(t, k) for t, k in ser if t >= w0], settle=w0)
        r[f'held_{name}'] = held
        if arm == 'on' and not held: r['reject'].append(f'{name} not held: {why} {det}')
    return r


def summary(results, intervals):
    lines = []
    for units in intervals:
        iv = units * 1250
        on = [r['agg'] for r in results if r['interval_us'] == iv and r['arm'] == 'on' and not r['reject']]
        off = [r['agg'] for r in results if r['interval_us'] == iv and r['arm'] == 'off' and not r['reject']]
        rej = sum(1 for r in results if r['interval_us'] == iv and r['reject'])
        if len(on) < 2 or len(off) < 2:
            lines.append(f'{iv/1000:>5} ms  on={on} off={off} rejected={rej} (too few reps for a delta)'); continue
        mo, mn = statistics.mean(off), statistics.mean(on)
        v1, v2 = statistics.variance(on) / len(on), statistics.variance(off) / len(off)
        df = (v1 + v2) ** 2 / (v1 ** 2 / (len(on) - 1) + v2 ** 2 / (len(off) - 1)) if v1 + v2 > 0 else len(on) + len(off) - 2
        ci = TCRIT.get(max(2, min(8, int(df))), 2.306) * math.sqrt(v1 + v2)
        sep = 'strict non-overlap' if min(on) > max(off) or max(on) < min(off) else 'ranges overlap'
        lines.append(f'{iv/1000:>5} ms  off {mo:6.1f} (n={len(off)}, {min(off)}-{max(off)})  on {mn:6.1f} (n={len(on)}, '
                     f'{min(on)}-{max(on)})  FSU {100*(mn-mo)/mo:+.1f}% ± {100*ci/mo:.1f}% (Welch t95)  {sep}  rejected={rej}')
        for arm in ('off', 'on'):
            pes = [(r['pe_mode'], r['pe_mean']) for r in results
                   if r['interval_us'] == iv and r['arm'] == arm and not r['reject'] and 'pe_mode' in r]
            if pes:
                lines.append(f'          {arm:>3}: exchanges/event mode {sorted({m for m, _ in pes})}  '
                             f'mean {statistics.mean(x for _, x in pes):.2f}')
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--stack', choices=['open', 'sdc'], required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--intervals', default='6,20', help='CONFIG_APP_CONN_INT_UNITS list (x1.25 ms)')
    ap.add_argument('--builds', default=None)
    ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--smoke', action='store_true', help='one rep per arm per interval')
    ap.add_argument('--diag', action='store_true', help='open only: add the per-event exchange counter')
    ap.add_argument('--cen'); ap.add_argument('--per')
    ap.add_argument('--ncs-root', default=os.environ.get('NCS_ROOT'))
    a = ap.parse_args()
    c = CFG[a.stack]; intervals = [int(x) for x in a.intervals.split(',')]
    if a.diag and a.stack != 'open':
        die('--diag needs the patched open controller (--stack open)')
    builds = a.builds or os.path.join(REPO, 'build', f'gatt-duplex-{a.stack}{"-diag" if a.diag else ""}')
    diag_flag = ['-DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y'] if a.diag else []
    if a.stack == 'sdc' and not a.skip_build and not a.ncs_root:
        die('--ncs-root (or NCS_ROOT) is required for SDC builds')

    if not a.skip_build:
        print(f'== building {a.stack} arms into {builds}', flush=True)
        for u in intervals:
            for arm in ('on', 'off'):
                build(a.stack, 'apps/nrf54l15/z54-lat-central', f'{builds}/c-{arm}-{u}',
                      ';'.join(c['cen'] + [c[arm]]), [f'-DCONFIG_APP_CONN_INT_UNITS={u}'] + diag_flag, a.ncs_root)
        build(a.stack, 'apps/nrf54l15/z54-lat-periph', f'{builds}/p-echo', c['per'],
              ['-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n'], a.ncs_root)

    print('== design gate', flush=True)
    for u in intervals:
        cfg = lambda arm: os.path.join(app_dir(a.stack, f'{builds}/c-{arm}-{u}', 'z54-lat-central'), 'zephyr', '.config')
        r = sh([sys.executable, os.path.join(REPO, 'tools', 'check_matched_pair.py'), cfg('on'), cfg('off'),
                '--allow', 'CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US'], timeout=60)
        print(f'  {u}: ' + (r.stdout.strip().splitlines() or ['?'])[-1], flush=True)
        if r.returncode: die('central arms are not a matched pair')
    pcfg = open(os.path.join(app_dir(a.stack, f'{builds}/p-echo', 'z54-lat-periph'), 'zephyr', '.config')).read()
    for want in c['per_checks'] + ['# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set']:
        if want not in pcfg: die(f'echo peripheral config lacks: {want}')
    print('  echo peripheral: FSU-capable, auto-update off', flush=True)

    if a.cen and a.per:
        (cen, ctty), (per, ptty) = a.cen.split(':', 1), a.per.split(':', 1)
    else:
        (cen, ctty), (per, ptty) = detect()
    print(f'  central {cen} {ctty} / peripheral {per} {ptty}', flush=True)

    hexp = lambda bdir, app: os.path.join(app_dir(a.stack, bdir, app), 'zephyr', 'zephyr.hex')
    caps = os.path.join(a.out, 'caps'); os.makedirs(caps, exist_ok=True)

    def flash(h, sn):
        r = sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
        if r.returncode: die(f'flash failed {h}: {r.stderr[-300:]}')

    flash(hexp(f'{builds}/p-echo', 'z54-lat-periph'), per)
    order = ['off', 'on'] if a.smoke else ORDER
    results = []
    for u in intervals:
        for i, arm in enumerate(order):
            tag = f'{a.stack}{u}_{arm}_r{i+1}'
            flash(hexp(f'{builds}/c-{arm}-{u}', 'z54-lat-central'), cen)
            p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(c['secs']),
                                  f'{ctty}:{tag}-cen', f'{ptty}:{tag}-per'],
                                 env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            sh(['nrfutil', 'device', 'reset', '--serial-number', per], timeout=60)
            sh(['nrfutil', 'device', 'reset', '--serial-number', cen], timeout=60)
            p.wait(timeout=c['secs'] + 60)
            r = measure(caps, tag, arm, u * 1250); r['stack'] = a.stack; results.append(r)
            print(json.dumps({k: r.get(k) for k in ('tag', 'spacing', 'onset_s', 'agg', 'pe_mode', 'pe_mean', 'reject')
                              if k in r}), flush=True)
            with open(os.path.join(a.out, 'results.jsonl'), 'a') as f: f.write(json.dumps(r) + '\n')
    print('\n=== SUMMARY (accepted reps only) ===')
    for line in summary(results, intervals): print(line)
    sys.exit(1 if any(r['reject'] for r in results) else 0)


if __name__ == '__main__':
    main()
