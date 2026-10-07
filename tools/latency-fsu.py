#!/usr/bin/env python3
"""Stop-signal latency under load, FSU on vs off (matched arms), GATT, open Zephyr or Nordic SDC.

Runs the existing z54-lat-central load ramp unchanged: a serialized 8-byte ping-pong stop-signal plus a
token-bucket 244-byte bulk stream stepping 0 -> 150 KB/s (7 stages x 60 s); the central prints one
`PCTL target=<KB/s> tot= to= p50= p90= p99= p99.9= max= g30= g100=` line per stage. The two arms differ
ONLY in the requested frame space (hh-fsu52.conf vs hh-fsu150.conf on top of the documented latency
recipe loadramp.conf;fsu-open.conf, 10-buffer queue); a ':polite' config adds CONFIG_APP_LOAD_POLITE=y
(completion-paced bulk). See REPRODUCE.md, "Latency under load with FSU".

  python3 tools/latency-fsu.py --out <dir> [--configs 6:naive,6:polite,20:naive] [--smoke]
  options: --builds <dir>  --skip-build  --cen ID:TTY --per ID:TTY  --abba N (ABBA blocks; 1 -> n=2 per arm)
  --high   high-load stages 0/140/150/160/170/180/190 KB/s (CONFIG_APP_LOAD_RAMP_HIGH=y) instead of 0..150
  python3 tools/latency-fsu.py --summarize <dir> [<dir> ...]     # pool results.jsonl from several runs
  --stack sdc --ncs-root <NCS v3.4.0>   Nordic SDC arms (hh-sdc-fsu / hh-sdc-nofsu; SDC TX packet count 10)
  --diag   open only: the controller's per-event transaction counter (CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG);
           each stage then also reports transactions per connection event

Each run is captured from boot (~7.5 min). FSU-on must log `FSU: updated ... spacing=52`; FSU-off the
`FSU: request [150..150] rc=0` line and never a reduced spacing; the interval must hold. Note: the idle
stage (target=0) includes the first ~6 s before FSU completes.
"""
import argparse, json, os, re, statistics, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link_gates  # noqa: E402

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
B = 'nrf54l15dk/nrf54l15/cpuapp'
ORDER = ['off', 'on', 'on', 'off']                   # ABBA -> n=2 per arm
SECS = 450
# 10-buffer ACL queue (loadramp.conf alone), as in the accepted 08-14 hardening latency result. The
# gdeep.conf variant (64 buffers, used deliberately by the 15 ms control/EATT recipes) censors the 7.5 ms
# tail at the 200 ms ping timeout, which hides any FSU difference (docs/LESSONS.md).
BASE = 'loadramp.conf;fsu-open.conf'
CFG = {
    'open': dict(cen=BASE, on='hh-fsu52.conf', off='hh-fsu150.conf', per=BASE),
    # SDC: tuned controller TX queue (default 3 starves throughput); FSU request is issued ~12 s after connect
    'sdc': dict(cen='sdc-sel.conf;loadramp.conf;sdc-llbuf.conf', on='hh-sdc-fsu.conf', off='hh-sdc-nofsu.conf',
                per='sdc-sel.conf;loadramp.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf'),
}


def sh(cmd, timeout=900, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, **kw)


def die(msg):
    sys.exit(f'ERROR: {msg}')


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
        die(f'need exactly 2 nRF54L15-DK, found {len(boards)}; pass --cen ID:TTY --per ID:TTY')
    return boards[0], boards[1]


def build(app, bdir, conf, extra, stack='open', ncs_root=None):
    args = ['west', 'build', '-p', 'always', '-b', B, os.path.join(REPO, app), '-d', bdir, '--',
            f'-DEXTRA_CONF_FILE={conf}'] + extra
    if stack == 'open':
        r = sh(args)
    else:
        env = dict(os.environ); env.pop('ZEPHYR_BASE', None)
        r = sh(['nrfutil', 'toolchain-manager', 'launch', '--ncs-version', 'v3.4.0', '--'] + args, cwd=ncs_root, env=env)
    if r.returncode: die(f'build failed {bdir}\n{r.stdout[-1500:]}{r.stderr[-600:]}')


def zdir(bdir, app, stack):          # SDC builds are sysbuild: the app image is nested
    return os.path.join(bdir, os.path.basename(app), 'zephyr') if stack == 'sdc' else os.path.join(bdir, 'zephyr')


def flash(h, sn):
    r = sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: die(f'flash failed {h}: {r.stderr[-300:]}')
    sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)   # program leaves the core halted


def measure(caps, tag, arm, units, stack='open'):
    r = {'tag': tag, 'arm': arm, 'interval_units': units, 'reject': [], 'stages': {}}
    try:
        lines = open(f'{caps}/{tag}-cen.log', errors='ignore').read().splitlines()
    except FileNotFoundError:
        r['reject'].append('capture file missing'); return r
    # start AFTER the last boot banner line: a reset can cut the previous boot's last line mid-print,
    # so the banner shares a line with a stale fragment (seen: 'interval=7*** Booting Zephyr ...')
    idx = max([i + 1 for i, l in enumerate(lines) if 'Booting Zephyr' in l] or [0]); lines = lines[idx:]
    r['reject'] += link_gates.phy_dle(lines) + link_gates.interval_held(lines, units)
    upd = [int(m.group(1)) for l in lines if 'FSU: updated' in l for m in [re.search(r'spacing=(\d+)', l)] if m]
    if arm == 'on':
        if not upd or (upd[-1] != 52 if stack == 'open' else upd[-1] >= 150):
            r['reject'].append(f'FSU-on spacing {upd[-1:] or "missing"}')
        elif upd: r['spacing'] = upd[-1]
    else:
        if any(x < 150 for x in upd): r['reject'].append('off-arm reached a reduced spacing')
        if not any('FSU: request [150..150]' in l and 'rc=0' in l for l in lines):
            r['reject'].append('no FSU request [150..150] rc=0')
    ivs = {int(m.group(1)) for l in lines for m in [re.search(r'interval=(\d+)', l)] if m}
    if ivs and ivs != {units * 1250}: r['reject'].append(f'interval {sorted(ivs)} != {units * 1250}')
    if not ivs: r['warn'] = 'no interval line captured'
    for l in lines:
        m = re.search(r'PCTL target=(\d+) tot=(\d+) to=(\d+) p50=(\d+) p90=(\d+) p99=(\d+) p99.9=(\d+) max=(\d+) g30=(\d+) g100=(\d+)', l)
        if m:
            t, tot, to, p50, p90, p99, p999, mx, g30, g100 = map(int, m.groups())
            r['stages'][t] = dict(tot=tot, to=to, p50=p50, p90=p90, p99=p99, p999=p999, max=mx,
                                  over30=round(g30 / tot, 4) if tot else None)
    if len(r['stages']) < 7: r['reject'].append(f'only {len(r["stages"])} load stages')
    # achieved bulk per stage = mean of the peripheral's per-second blkkBps inside the stage (skip 10 s
    # settle, 2 s tail); both logs share the capture clock. Stages above the link's ceiling fall short.
    try:
        per = [(float(m.group(1)), int(m.group(2))) for l in open(f'{caps}/{tag}-per.log', errors='ignore')
               for m in [re.match(r'\s*([\d.]+)\s+P t=\d+s .*blkkBps=(\d+)', l)] if m]
        marks = [(float(m.group(1)), int(m.group(2))) for l in lines
                 for m in [re.match(r'\s*([\d.]+)\s+LOADRAMP: target=(\d+)', l)] if m]
        for i, (t0, tg) in enumerate(marks):
            t1 = marks[i + 1][0] if i + 1 < len(marks) else t0 + 60
            v = [b for t, b in per if t0 + 10 < t < t1 - 2]
            if v and tg in r['stages']: r['stages'][tg]['bulk_kbps'] = round(statistics.mean(v), 1)
    except FileNotFoundError:
        r['warn'] = 'no peripheral log (achieved bulk unknown)'
    # --diag: controller transactions per connection event, summed over each stage's 2 s windows
    pe = []
    for l in lines:
        m = re.match(r'\s*([\d.]+)\s+RPT pe: events=(\d+) .*?\|(.*)', l)
        if m: pe.append((float(m.group(1)), {int(k): int(v) for k, v in re.findall(r'(\d+):(\d+)', m.group(3))}))
    if pe:
        marks = [(float(m.group(1)), int(m.group(2))) for l in lines
                 for m in [re.match(r'\s*([\d.]+)\s+LOADRAMP: target=(\d+)', l)] if m]
        for i, (t0, tg) in enumerate(marks):
            t1 = marks[i + 1][0] if i + 1 < len(marks) else t0 + 60
            h = {}
            for t, d in pe:
                if t0 + 10 < t < t1 - 2:
                    for k, v in d.items(): h[k] = h.get(k, 0) + v
            tot = sum(h.values())
            if tot and tg in r['stages']:
                big = [k for k in sorted(h) if h[k] / tot >= 0.05]
                r['stages'][tg].update(pe_mean=round(sum(k * v for k, v in h.items()) / tot, 2),
                                       pe_full=max(big) if big else max(h),
                                       pe_full_share=round(h[max(big)] / tot, 3) if big else None)
    return r


def summary(results, configs):
    out = []
    for units, mode in configs:
        rs = [r for r in results if r['interval_units'] == units and r['mode'] == mode and not r['reject']]
        out.append(f'--- {units * 1.25} ms, {mode} (accepted on={sum(r["arm"]=="on" for r in rs)} off={sum(r["arm"]=="off" for r in rs)})')
        for t in sorted({int(k) for r in rs for k in r['stages']}):
            row = []
            for arm in ('off', 'on'):
                st = [r['stages'][str(t)] if str(t) in r['stages'] else r['stages'][t] for r in rs
                      if r['arm'] == arm and (t in r['stages'] or str(t) in r['stages'])]
                if st:
                    row.append(f'{arm}: p50 {statistics.mean(s["p50"] for s in st):5.1f}  p99 {statistics.mean(s["p99"] for s in st):5.1f}  '
                               f'p99.9 {statistics.mean(s["p999"] for s in st):5.1f}  >30ms {100*statistics.mean(s["over30"] for s in st):5.1f}%  '
                               f'timeouts {sum(s["to"] for s in st)}'
                               + (f'  bulk {statistics.mean(s["bulk_kbps"] for s in st if "bulk_kbps" in s):5.1f}'
                                  if any("bulk_kbps" in s for s in st) else '')
                               + (f'  tx/event {statistics.mean(s["pe_mean"] for s in st if "pe_mean" in s):4.2f}'
                                  f' full {sorted({s["pe_full"] for s in st if "pe_full" in s})}'
                                  if any("pe_mean" in s for s in st) else ''))
            out.append(f'  load {t:>3} KB/s | ' + ' | '.join(row))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out')
    ap.add_argument('--abba', type=int, default=1, help='ABBA blocks per config (1 -> n=2 per arm)')
    ap.add_argument('--high', action='store_true', help='high-load stages (CONFIG_APP_LOAD_RAMP_HIGH=y)')
    ap.add_argument('--summarize', nargs='+', metavar='DIR', help='pool results.jsonl from run dirs, no hardware')
    ap.add_argument('--stack', choices=['open', 'sdc'], default='open')
    ap.add_argument('--ncs-root', default=os.environ.get('NCS_ROOT'))
    ap.add_argument('--diag', action='store_true', help='open only: per-event transaction counter')
    ap.add_argument('--configs', default='6:naive,6:polite,20:naive')
    ap.add_argument('--builds'); ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--smoke', action='store_true', help='one rep per arm')
    ap.add_argument('--cen'); ap.add_argument('--per')
    a = ap.parse_args()
    if a.summarize:
        results = []
        for d in a.summarize:                 # re-measure from the archived captures with the current gates
            for old in (json.loads(l) for l in open(os.path.join(d, 'results.jsonl'))):
                r = measure(os.path.join(d, 'caps'), old['tag'], old['arm'], old['interval_units'], old.get('stack', 'open'))
                r['mode'] = old['mode']; results.append(r)
                if r['reject']: print(f'  rejected {d}/{old["tag"]}: {r["reject"]}')
        cfgs = sorted({(r['interval_units'], r['mode']) for r in results})
        for line in summary(results, cfgs): print(line)
        return
    if not a.out: die('--out is required')
    if a.diag and a.stack != 'open': die('--diag needs the open controller')
    if a.stack == 'sdc' and not a.skip_build and not a.ncs_root: die('--ncs-root (or NCS_ROOT) is required for SDC builds')
    c = CFG[a.stack]
    CEN, PER = 'apps/nrf54l15/z54-lat-central', 'apps/nrf54l15/z54-lat-periph'
    configs = [(int(c.split(':')[0]), c.split(':')[1]) for c in a.configs.split(',')]
    builds = a.builds or os.path.join(REPO, 'build', 'latency-fsu')
    if not a.skip_build:
        print(f'== building into {builds}', flush=True)
        for units, mode in configs:
            extra = ([f'-DCONFIG_APP_CONN_INT_UNITS={units}'] + (['-DCONFIG_APP_LOAD_POLITE=y'] if mode == 'polite' else [])
                     + (['-DCONFIG_APP_LOAD_RAMP_HIGH=y'] if a.high else [])
                     + (['-DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y'] if a.diag else []))
            for arm in ('on', 'off'):
                build(CEN, f'{builds}/c-{arm}-{units}-{mode}', f'{c["cen"]};{c[arm]}', extra, a.stack, a.ncs_root)
        build(PER, f'{builds}/periph', c['per'], ['-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n'], a.stack, a.ncs_root)
    print('== design gate', flush=True)
    for units, mode in configs:
        cfg = lambda arm: os.path.join(zdir(f'{builds}/c-{arm}-{units}-{mode}', CEN, a.stack), '.config')
        g = sh([sys.executable, os.path.join(REPO, 'tools', 'check_matched_pair.py'), cfg('on'), cfg('off'),
                '--allow', 'CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US'], timeout=60)
        print(f'  {units}/{mode}: ' + (g.stdout.strip().splitlines() or ['?'])[-1], flush=True)
        if g.returncode: die('arms are not a matched pair')
    pcfg = open(os.path.join(zdir(f'{builds}/periph', PER, a.stack), '.config')).read()
    fsu_line = 'CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US=52' if a.stack == 'open' else 'CONFIG_BT_CTLR_SDC_ENABLE_LOWEST_FRAME_SPACE=y'
    for want in (fsu_line, '# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set'):
        if want not in pcfg: die(f'peripheral config lacks: {want}')
    (cen, ctty), (per, ptty) = (a.cen.split(':', 1), a.per.split(':', 1)) if a.cen and a.per else detect()
    print(f'  central {cen} {ctty} / peripheral {per} {ptty}', flush=True)
    caps = os.path.join(a.out, 'caps'); os.makedirs(caps, exist_ok=True)
    flash(os.path.join(zdir(f'{builds}/periph', PER, a.stack), 'zephyr.hex'), per)
    results = []
    for units, mode in configs:
        for i, arm in enumerate(['off', 'on'] if a.smoke else ORDER * a.abba):
            tag = f'{"sdc" if a.stack == "sdc" else ""}lat{units}{mode}{"high" if a.high else ""}_{arm}_r{i+1}'
            flash(os.path.join(zdir(f'{builds}/c-{arm}-{units}-{mode}', CEN, a.stack), 'zephyr.hex'), cen)
            p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(SECS),
                                  f'{ctty}:{tag}-cen', f'{ptty}:{tag}-per'],
                                 env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            sh(['nrfutil', 'device', 'reset', '--serial-number', per], timeout=60)
            sh(['nrfutil', 'device', 'reset', '--serial-number', cen], timeout=60)
            p.wait(timeout=SECS + 90)
            r = measure(caps, tag, arm, units, a.stack); r['mode'] = mode + ('-high' if a.high else ''); r['stack'] = a.stack
            results.append(r)
            s150 = r['stages'].get(150, {})
            print(json.dumps({'tag': tag, 'stages': len(r['stages']), 'p99@150': s150.get('p99'),
                              '>30@150': s150.get('over30'), 'reject': r['reject']}), flush=True)
            with open(os.path.join(a.out, 'results.jsonl'), 'a') as f: f.write(json.dumps(r) + '\n')
    print('\n=== SUMMARY (accepted reps; mean over reps) ===')
    for line in summary(results, [(u, m + ('-high' if a.high else '')) for u, m in configs]): print(line)
    sys.exit(1 if any(r['reject'] for r in results) else 0)


if __name__ == '__main__':
    main()
