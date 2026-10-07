#!/usr/bin/env python3
"""One-way throughput, FSU off vs on, open Zephyr vs Nordic SDC, GATT and L2CAP CoC: one same-session,
interleaved, equally tuned campaign whose cross-stack absolute rates are directly comparable.

Why: earlier one-way campaigns measured each stack in its own session with unequal controller tuning
(e.g. the September GATT SDC receiver had 10 RX buffers and the default event length), so FSU gains were
valid but Zephyr-vs-SDC absolute gaps were not. This tool fixes all three: same session, interleaved
order, one documented tuning profile applied to both stacks and verified in the resolved configs.

  python3 tools/oneway-fsu.py --out <dir> --ncs-root <NCS v3.4.0>            # headroom check + full sweep
  options: --intervals 6,12,20,30,40  --reps 4  --builds <dir>  --skip-build  --smoke
           --phase headroom|sweep|both (default both)  --cen ID:TTY --per ID:TTY

Tuning profiles (applied on top of each recipe's own overlays, as -D flags; the resolved .config of every
image is checked for them before anything is flashed):
  max  both stacks: senders keep >= 20 host ACL TX buffers (CoC recipes already use 64); receivers get
       BT_BUF_ACL_RX_COUNT_EXTRA=20 (GATT senders also get 24 event RX buffers, which the host requires to exceed
       the ACL TX count). Open controller: BT_CTLR_RX_BUFFERS=18 (its maximum) on both boards.
       SDC: TX/RX packet count 20 (its maximum), max connection-event length 50 ms + event extension, both
       boards. Event length is otherwise unrestricted on the open controller.
  std  each recipe as used in the September campaigns (headroom comparison only).
Headroom phase: std vs max, FSU on, at 7.5 and 50 ms, both transports and stacks, alternating, n=2. If max
does not exceed std the recipes were not buffer-limited; the sweep always uses max.

Sweep: rounds r=1..reps; each round visits every (interval, transport, stack, arm) cell with Zephyr and
SDC adjacent in time; even rounds run the order reversed (drift cancels). Per rep: flash, open capture,
reset both boards, 32 s capture. Throughput = receiver-delivered median over the last 12 s, clipped to
start after FSU took effect (+1 s). Gates per rep: explicit FSU check (on: reduced spacing logged; off:
the 150 us request, never a reduced spacing), tools/verify_run.verify (FSU held, interval, live link).
Design gates before flashing: matched on/off pairs (only APP_FSU_MIN/MAX differ), host parity between the
two stacks' images of the same transport, the tuning profile present on both boards, FSU-capable sinks
with connection-parameter auto-update off. Summary: per cell off/on means, FSU gain (paired by round,
Student-t 95%), and the Zephyr-minus-SDC difference per arm (paired by round, Student-t 95%).
"""
import argparse, json, math, os, re, statistics, subprocess, sys, time

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from verify_run import verify            # noqa: E402
import link_gates                         # noqa: E402
B = 'nrf54l15dk/nrf54l15/cpuapp'
SECS = 32
TCRIT = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 11: 2.20}
AUTO_OFF = 'CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n'

APPS = {'gatt': ('apps/nrf54l15/z54-lat-central', 'apps/nrf54l15/z54-lat-periph'),
        'coc': ('apps/coc/coc-central', 'apps/coc/coc-sink')}
RECIPE = {   # each transport x stack, exactly as documented in REPRODUCE (centrals: + arm overlay)
    ('gatt', 'open'): dict(cen='tput-open.conf;fsu-open.conf', on='hh-fsu52.conf', off='hh-fsu150.conf',
                           sink='tput-open.conf;fsu-open.conf', cen_d=[], sink_d=[AUTO_OFF]),
    ('gatt', 'sdc'): dict(cen='sdc-sel.conf;tput.conf', on='hh-sdc-fsu.conf', off='hh-sdc-nofsu.conf',
                          sink='sdc-sel.conf;tput.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf', cen_d=[], sink_d=[AUTO_OFF]),
    ('coc', 'open'): dict(cen='', on='open-fsu.conf', off='open-nofsu.conf',
                          sink='open-fsu.conf', cen_d=['CONFIG_APP_SDU_SIZE=480'], sink_d=[AUTO_OFF]),
    ('coc', 'sdc'): dict(cen='sdc-sel.conf', on='sdc-fsu.conf', off='sdc-nofsu.conf',
                         sink='sdc-sel.conf;sdc.conf', cen_d=['CONFIG_APP_SDU_SIZE=480'], sink_d=[AUTO_OFF]),
}
SDC_MAX = ['CONFIG_BT_CTLR_SDC_TX_PACKET_COUNT=20', 'CONFIG_BT_CTLR_SDC_RX_PACKET_COUNT=20',
           'CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT_OVERRIDE=y',
           'CONFIG_BT_CTLR_SDC_MAX_CONN_EVENT_LEN_DEFAULT=50000', 'CONFIG_BT_CTLR_SDC_CONN_EVENT_EXTEND_DEFAULT=y']
OPEN_MAX = ['CONFIG_BT_CTLR_RX_BUFFERS=18']
def profile_flags(profile, transport, stack, role):
    if profile == 'std':
        return []
    f = list(SDC_MAX if stack == 'sdc' else OPEN_MAX)
    if role == 'cen' and transport == 'gatt':      # CoC centrals already carry 64 ACL/L2CAP TX buffers
        f += ['CONFIG_BT_BUF_ACL_TX_COUNT=20', 'CONFIG_BT_CONN_TX_MAX=20', 'CONFIG_BT_L2CAP_TX_BUF_COUNT=20',
              'CONFIG_BT_BUF_EVT_RX_COUNT=24']   # the host requires event RX buffers > ACL TX buffers
    if role == 'sink':
        f += ['CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA=20']
    return f
HOST_PARITY = ['CONFIG_BT_BUF_ACL_TX_COUNT', 'CONFIG_BT_BUF_ACL_TX_SIZE', 'CONFIG_BT_BUF_ACL_RX_SIZE',
               'CONFIG_BT_L2CAP_TX_MTU', 'CONFIG_BT_L2CAP_TX_BUF_COUNT', 'CONFIG_BT_CONN_TX_MAX',
               'CONFIG_APP_SDU_SIZE', 'CONFIG_APP_CONN_INT_UNITS', 'CONFIG_APP_TPUT_BLAST',
               'CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA', 'CONFIG_BT_L2CAP_SEG_RECV', 'CONFIG_BT_AUTO_DATA_LEN_UPDATE',
               'CONFIG_BT_BUF_EVT_RX_COUNT']


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


def build(app, bdir, conf, flags, stack, ncs_root):
    args = ['west', 'build', '-p', 'always', '-b', B, os.path.join(REPO, app), '-d', bdir, '--']
    if conf: args.append(f'-DEXTRA_CONF_FILE={conf}')
    args += [f'-D{f}' for f in flags]
    if stack == 'open':
        r = sh(args)
    else:
        env = dict(os.environ); env.pop('ZEPHYR_BASE', None)
        r = sh(['nrfutil', 'toolchain-manager', 'launch', '--ncs-version', 'v3.4.0', '--'] + args, cwd=ncs_root, env=env)
    if r.returncode: die(f'build failed {bdir}\n{r.stdout[-1500:]}{r.stderr[-600:]}')


def zdir(bdir, app, stack):
    return os.path.join(bdir, os.path.basename(app), 'zephyr') if stack == 'sdc' else os.path.join(bdir, 'zephyr')


def img(builds, profile, t, st, role, arm=None, u=None):
    name = f'{profile}/{t}-{st}/' + (f'c-{arm}-{u}' if role == 'cen' else 'sink')
    app = APPS[t][0 if role == 'cen' else 1]
    return os.path.join(builds, name), app


def cfgmap(path):
    d = {}
    for l in open(path):
        m = re.match(r'(CONFIG_\w+)=(.*)', l.strip())
        if m: d[m.group(1)] = m.group(2)
        m = re.match(r'# (CONFIG_\w+) is not set', l.strip())
        if m: d[m.group(1)] = 'n'
    return d


def flash(h, sn):
    r = sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn], timeout=180)
    if r.returncode: return False
    sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)   # program leaves the core halted
    return True


def fsu_lines(cen_log, transport):
    """(onset_s, last reduced spacing or None, off-request seen) from the central log after the last boot."""
    try: L = open(cen_log, errors='ignore').read().splitlines()
    except FileNotFoundError: return None, None, False
    i = max([k + 1 for k, l in enumerate(L) if 'Booting Zephyr' in l] or [0]); L = L[i:]
    onset = sp = None; offreq = False
    for l in L:
        m = re.match(r'\s*([\d.]+)\s+(.*)', l)
        if not m: continue
        t, body = float(m.group(1)), m.group(2)
        mm = re.search(r'(FSU: updated|Q3FSU-DONE role=C).*?spacing=(\d+)', body)
        if mm:
            onset, sp = t, int(mm.group(2))
        if (('FSU: request [150..150]' in body) or ('Q3FSU-REQ role=C' in body and 'min=150 max=150' in body)) and 'rc=0' in body:
            offreq = True; onset = onset if onset is not None else t
    return onset, sp, offreq


def late_throughput(per_log, onset, win=12.0, settle=1.0, min_pts=5):
    s, cum = [], []
    try:
        for l in open(per_log, errors='ignore'):
            m = re.search(r'^\s*([0-9.]+)\s+P t=\d+s rxkBps=([0-9]+)', l) or re.search(r'^\s*([0-9.]+)\s+SINK rx:\s*([0-9]+)', l)
            if m: s.append((float(m.group(1)), int(m.group(2))))
            c = re.search(r'^\s*([0-9.]+)\s+SINK rx:.*cum_total=(\d+) B', l)
            if c: cum.append((float(c.group(1)), int(c.group(2))))
    except FileNotFoundError:
        return 0, None, 0, False
    if not s: return 0, None, 0, False
    start = s[-1][0] - win
    if onset is not None: start = max(start, onset + settle)
    w = [v for t, v in s if t >= start and v > 0]
    # CoC sinks: use the cumulative byte counter's slope. Their per-second line is integer-truncated and its period is
    # ~1.009 s (k_msleep + blocking printk), so its median reads ~1% high (GATT's line period is 1.000 s; kept as is).
    cw = [(t, b) for t, b in cum if t >= start]
    if len(cw) >= min_pts and cw[-1][0] > cw[0][0]:
        return round((cw[-1][1] - cw[0][1]) / 1024 / (cw[-1][0] - cw[0][0]), 1), round(start, 1), len(w), len(w) >= min_pts
    return (statistics.median(w) if w else 0), round(start, 1), len(w), len(w) >= min_pts


def measure(caps, tag, t, st, arm, u):
    r = {'tag': tag, 'transport': t, 'stack': st, 'arm': arm, 'interval_units': u, 'reject': []}
    cen_log, per_log = f'{caps}/{tag}-cen.log', f'{caps}/{tag}-per.log'
    if not (os.path.exists(cen_log) and os.path.exists(per_log)):
        r['reject'].append('capture file missing'); return r
    onset, sp, offreq = fsu_lines(cen_log, t)
    if arm == 'on':
        if sp is None or sp >= 150: r['reject'].append(f'FSU-on: no reduced spacing logged (spacing={sp})')
        if st == 'open' and sp is not None and sp != 52: r['reject'].append(f'open FSU-on spacing {sp} != 52')
    else:
        if sp is not None and sp < 150: r['reject'].append(f'FSU-off arm reached spacing {sp}')
        if not offreq: r['reject'].append('FSU-off: no 150 us request logged')
    r['spacing'] = sp if arm == 'on' else 150
    kbps, wstart, n, ok = late_throughput(per_log, onset)
    r.update(kbps=kbps, window_start=wstart, window_n=n, onset_s=onset)
    if not ok: r['reject'].append(f'window not post-FSU / too short (onset={onset}, n={n})')
    v = verify(per_log, cen_log, fsu=arm, stack=st, interval_ms=u * 1.25, min_kbps=5)
    def after_boot(path):
        L = open(path, errors='ignore').read().splitlines()
        return L[max([i + 1 for i, l in enumerate(L) if 'Booting Zephyr' in l] or [0]):]
    cl, pl = after_boot(cen_log), after_boot(per_log)
    r['reject'] += link_gates.phy_dle(cl, duplex=False) + link_gates.interval_held(cl, u)
    if t == 'coc': r['reject'] += link_gates.credit_window(cl, pl)
    r['reject'] += v.get('rejects', [])
    return r


def tstat(xs):
    n = len(xs)
    if n < 2: return (statistics.mean(xs) if xs else float('nan')), float('nan'), n
    return statistics.mean(xs), TCRIT.get(n - 1, 2.0) * statistics.stdev(xs) / math.sqrt(n), n


def summary(results, intervals, profile='max'):
    rs = [r for r in results if r.get('profile', 'max') == profile and r.get('phase', 'sweep') == 'sweep']
    acc = {}
    for r in rs:
        if not r['reject']:
            acc.setdefault((r['transport'], r['stack'], r['interval_units'], r['arm']), {})[r['round']] = r['kbps']
    out = ['== one-way FSU, same session, equal tuning (profile max); KB/s receiver-delivered']
    for t in [x for x in ('gatt', 'coc') if any(r['transport'] == x for r in rs)]:
        out.append(f'-- {t.upper()}')
        out.append('  interval | Zephyr off -> on (FSU gain)          | SDC off -> on (FSU gain)             | Zephyr - SDC (off / on)')
        for u in intervals:
            row = f'  {u*1.25:6.2f}ms |'
            cell = {}
            for st in ('open', 'sdc'):
                off, on = acc.get((t, st, u, 'off'), {}), acc.get((t, st, u, 'on'), {})
                rounds = sorted(set(off) & set(on))
                g = [100 * (on[k] / off[k] - 1) for k in rounds]
                m, ci, n = tstat(g)
                cell[st] = (off, on)
                row += (f' {statistics.mean(off.values()) if off else float("nan"):5.1f} -> {statistics.mean(on.values()) if on else float("nan"):5.1f}'
                        f' ({m:+5.1f}% ±{ci:4.1f}, n={n}) |')
            diffs = []
            for arm_i, arm in enumerate(('off', 'on')):
                a, b = cell['open'][arm_i], cell['sdc'][arm_i]
                d = [a[k] - b[k] for k in sorted(set(a) & set(b))]
                m, ci, n = tstat(d)
                diffs.append(f'{m:+5.1f}±{ci:4.1f}')
            row += f' {diffs[0]} / {diffs[1]}'
            out.append(row)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True); ap.add_argument('--ncs-root', default=os.environ.get('NCS_ROOT'))
    ap.add_argument('--intervals', default='6,12,20,30,40'); ap.add_argument('--reps', type=int, default=4)
    ap.add_argument('--builds'); ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--phase', choices=['headroom', 'sweep', 'both'], default='both')
    ap.add_argument('--smoke', action='store_true', help='one interval (20), one round, no headroom phase')
    ap.add_argument('--cen'); ap.add_argument('--per')
    ap.add_argument('--transports', default='gatt,coc', help='subset of gatt,coc')
    ap.add_argument('--coc-credit-batch', action='store_true',
                    help='CoC sinks return credits in batches (CONFIG_APP_CREDIT_BATCH=y) instead of one credit PDU per '
                         'segment; the per-segment default costs the 6th exchange at 7.5 ms with FSU (LESSONS)')
    a = ap.parse_args()
    intervals = [20] if a.smoke else [int(x) for x in a.intervals.split(',')]
    reps = 1 if a.smoke else a.reps
    phases = ['sweep'] if a.smoke else (['headroom', 'sweep'] if a.phase == 'both' else [a.phase])
    head_iv = [x for x in (6, 40) if x in intervals] or intervals[:1]
    builds = a.builds or os.path.join(REPO, 'build', 'oneway-fsu')
    os.makedirs(a.out, exist_ok=True)
    T, S = tuple(t for t in ('gatt', 'coc') if t in a.transports.split(',')), ('open', 'sdc')
    need = []   # (profile, t, st, role, arm, u)
    for t in T:
        for st in S:
            need.append(('max', t, st, 'sink', None, None))
            for u in intervals:
                for arm in ('off', 'on'):
                    need.append(('max', t, st, 'cen', arm, u))
            if 'headroom' in phases:
                need.append(('std', t, st, 'sink', None, None))
                for u in head_iv:
                    need.append(('std', t, st, 'cen', 'on', u))
                    need.append(('std', t, st, 'cen', 'off', u))     # matched-pair gate needs the pair
    if not a.skip_build:
        if not a.ncs_root: die('--ncs-root (or NCS_ROOT) is required for the SDC builds')
        print(f'== building {len(need)} images into {builds}', flush=True)
        for prof, t, st, role, arm, u in need:
            bdir, app = img(builds, prof, t, st, role, arm, u)
            rc = RECIPE[(t, st)]
            if role == 'cen':
                conf = ';'.join(x for x in (rc['cen'], rc[arm]) if x)
                flags = rc['cen_d'] + [f'CONFIG_APP_CONN_INT_UNITS={u}'] + profile_flags(prof, t, st, 'cen')
            else:
                conf, flags = rc['sink'], rc['sink_d'] + profile_flags(prof, t, st, 'sink')
                if t == 'coc' and a.coc_credit_batch: flags = flags + ['CONFIG_APP_CREDIT_BATCH=y']
            build(app, bdir, conf, flags, st, a.ncs_root)
            print(f'  built {prof}/{t}-{st}/{"c-"+arm+"-"+str(u) if role=="cen" else "sink"}', flush=True)

    # ---- design gates (nothing is flashed unless all pass) ----
    print('== design gates', flush=True)
    fails = []
    C = lambda prof, t, st, role, arm=None, u=None: os.path.join(zdir(img(builds, prof, t, st, role, arm, u)[0], img(builds, prof, t, st, role, arm, u)[1], st), '.config')
    for prof, t, st, role, arm, u in need:
        if role == 'cen' and arm == 'on':
            g = sh([sys.executable, os.path.join(REPO, 'tools', 'check_matched_pair.py'), C(prof, t, st, 'cen', 'on', u),
                    C(prof, t, st, 'cen', 'off', u), '--allow', 'CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US', '--quiet'], timeout=60)
            if g.returncode: fails.append(f'{prof}/{t}-{st} {u}: on/off not a matched pair')
        cfg = cfgmap(C(prof, t, st, role, arm, u))
        for f in profile_flags(prof, t, st, role):
            k, v = f.split('=', 1)
            if cfg.get(k) != v: fails.append(f'{prof}/{t}-{st} {role}{"-"+str(u) if u else ""}: {k}={cfg.get(k)} (profile wants {v})')
        if role == 'sink':
            if cfg.get('CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS') != 'n': fails.append(f'{prof}/{t}-{st} sink: auto-update not off')
            if not (cfg.get('CONFIG_BT_CTLR_SDC_ENABLE_LOWEST_FRAME_SPACE') == 'y' or cfg.get('CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US') == '52'):
                fails.append(f'{prof}/{t}-{st} sink: no FSU floor')
    for prof in sorted({n[0] for n in need}):
        for t in T:     # host parity: the two stacks' images of one transport must agree on every host setting
            pairs = [('sink', None, None)] + [('cen', arm, u) for (p, tt, st, role, arm, u) in need
                                               if p == prof and tt == t and st == 'open' and role == 'cen']
            for role, arm, u in pairs:
                o, d = cfgmap(C(prof, t, 'open', role, arm, u)), cfgmap(C(prof, t, 'sdc', role, arm, u))
                for k in HOST_PARITY:
                    if o.get(k) != d.get(k): fails.append(f'{prof}/{t} {role}{"-"+arm+"-"+str(u) if u else ""}: host {k} open={o.get(k)} sdc={d.get(k)}')
    if fails:
        print('DESIGN GATE FAILED — nothing flashed:'); [print('  ' + f) for f in sorted(set(fails))]; sys.exit(1)
    print(f'  {len(need)} images: matched pairs, profile present on both boards, host parity across stacks, FSU-capable sinks — OK', flush=True)
    with open(os.path.join(a.out, 'design-gate.txt'), 'w') as f:
        f.write(f'PASS {time.strftime("%F %T")}: matched pairs, tuning profile on both boards, host parity across stacks, sinks capable\n')

    (cen, ctty), (per, ptty) = (a.cen.split(':', 1), a.per.split(':', 1)) if a.cen and a.per else detect()
    print(f'  central {cen} {ctty} / peripheral {per} {ptty}', flush=True)
    caps = os.path.join(a.out, 'caps'); os.makedirs(caps, exist_ok=True)
    results = []
    loaded = {'cen': None, 'per': None}

    def run(prof, t, st, arm, u, rnd, phase):
        tag = f'{phase}-{prof}-{t}-{st}-{u}-{arm}-r{rnd}'
        sdir, sapp = img(builds, prof, t, st, 'sink'); cdir, capp = img(builds, prof, t, st, 'cen', arm, u)
        shex, chex = os.path.join(zdir(sdir, sapp, st), 'zephyr.hex'), os.path.join(zdir(cdir, capp, st), 'zephyr.hex')
        ok = True
        if loaded['per'] != shex: ok = flash(shex, per) and ok; loaded['per'] = shex
        if loaded['cen'] != chex: ok = flash(chex, cen) and ok; loaded['cen'] = chex
        if not ok:
            r = {'tag': tag, 'transport': t, 'stack': st, 'arm': arm, 'interval_units': u, 'reject': ['flash failed']}
            loaded['per'] = loaded['cen'] = None
        else:
            p = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), str(SECS),
                                  f'{ctty}:{tag}-cen', f'{ptty}:{tag}-per'],
                                 env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            sh(['nrfutil', 'device', 'reset', '--serial-number', per], timeout=60)
            sh(['nrfutil', 'device', 'reset', '--serial-number', cen], timeout=60)
            try: p.wait(timeout=SECS + 60)
            except subprocess.TimeoutExpired: p.kill()
            r = measure(caps, tag, t, st, arm, u)
        r.update(profile=prof, round=rnd, phase=phase)
        results.append(r)
        print(json.dumps({k: r.get(k) for k in ('tag', 'kbps', 'spacing', 'onset_s', 'reject')}), flush=True)
        with open(os.path.join(a.out, 'results.jsonl'), 'a') as f: f.write(json.dumps(r) + '\n')

    if 'headroom' in phases:
        print('== headroom: std vs max, FSU on', flush=True)
        cells = [(prof, t, st, 'on', u) for u in head_iv for t in T for st in S for prof in ('std', 'max')]
        for rnd in (1, 2):
            for c in (cells if rnd == 1 else list(reversed(cells))): run(*c, rnd, 'headroom')
        out = ['== headroom (FSU on; max vs std, mean of 2 reps)']
        for u in head_iv:
            for t in T:
                for st in S:
                    v = {prof: [r['kbps'] for r in results if r['phase'] == 'headroom' and r['profile'] == prof and r['transport'] == t
                                and r['stack'] == st and r['interval_units'] == u and not r['reject']] for prof in ('std', 'max')}
                    if v['std'] and v['max']:
                        out.append(f'  {u*1.25:5.2f}ms {t:4s} {st:4s}: std {statistics.mean(v["std"]):6.1f}  max {statistics.mean(v["max"]):6.1f}  '
                                   f'({100*(statistics.mean(v["max"])/statistics.mean(v["std"])-1):+5.1f}%)')
                    else:
                        out.append(f'  {u*1.25:5.2f}ms {t:4s} {st:4s}: insufficient accepted reps')
        print('\n'.join(out), flush=True)
        open(os.path.join(a.out, 'headroom.txt'), 'w').write('\n'.join(out) + '\n')

    if 'sweep' in phases:
        print('== sweep (profile max), interleaved', flush=True)
        cells = [('max', t, st, arm, u) for u in intervals for t in T for st in S for arm in ('off', 'on')]
        for rnd in range(1, reps + 1):
            for c in (cells if rnd % 2 else list(reversed(cells))): run(*c, rnd, 'sweep')
            s = summary(results, intervals)
            open(os.path.join(a.out, 'summary.txt'), 'w').write('\n'.join(s) + f'\n(after round {rnd} of {reps})\n')
        print('\n'.join(summary(results, intervals)))
    sys.exit(1 if any(r['reject'] for r in results) else 0)


if __name__ == '__main__':
    main()
