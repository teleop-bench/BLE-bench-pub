#!/usr/bin/env python3
"""On-air check of GATT echo duplex FSU with the nRF52 observer: packets per connection event and the
in-event gap, measured from the air rather than reported by the controller.

Rig: 1x nRF52832 DK (PCA10040) running apps/nrf52/pca10040-radio-observer (2M build:
-DQ2=1 -DAIRTIME_MIN_TICKS=256) + 2x nRF54L15-DK running the GATT duplex images from
tools/gatt-duplex-fsu.py (--stack open; c-<arm>-<units> + p-echo build dirs). The open controller
prints `Q2CONN AA=.. CRCINIT=..` on every connection; the observer is tuned to that AA/CRC on data
channel 10 and records every packet it sees there for 30 s (with 37-channel hopping the link visits
channel 10 about once every 37 events, so each capture holds tens of whole events).

  python3 tools/onair-duplex.py --builds <gatt-duplex open build dir> --obs-build <observer build> --out <dir>
  python3 tools/onair-duplex.py --mode coc --builds <coc-duplex open build dir> --obs-build <...> --out <dir>
  python3 tools/onair-duplex.py --reanalyze <out dir> [<out dir> ...]   # recompute from archived logs

--mode coc observes the CoC duplex link (coc-duplex-central + coc-duplex-sink, open stack; build dir from
tools/coc-duplex-fsu.py --stack open). As in that tool, the first connection after each central boot is
discarded and every rep resets only the peripheral (reconnect-wedge workaround).
  options: --intervals 6,12,20  --obs ID:TTY --cen ID:TTY --per ID:TTY

Per event (records split where the gap exceeds 4 ms) it counts packets; an event is "complete" only if
every in-event gap is short (no hidden missed packet), and only complete events are used. It reports
packets/event and exchanges/event (= packets / 2 in echo duplex) and the median in-event gap (next
ADDRESS - previous END; at 2M this is tIFS + ~24 µs of preamble and access address).
"""
import argparse, json, os, re, statistics, subprocess, sys, threading, time
import serial

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
B = 'nrf54l15dk/nrf54l15/cpuapp'
ORDER = ['off', 'on', 'on', 'off']
CONN_ATTEMPTS = 3                  # connections tried per rep when the Q2CONN line is missing
TICK_US = 1 / 16.0
EVENT_SPLIT_US = 4000      # gap between events on one channel >= the interval (>= 7.5 ms)
MIN_PKT_US = 44            # shortest packet on air (empty PDU at 2M): a missed packet adds >= this + one more gap
# Completeness gate (per rep): an in-event gap above the midpoint between a normal gap (the rep's median, i.e. the
# frame space) and a gap that hides one missed packet (2 * median + MIN_PKT_US) means a packet was missed. A fixed
# 400 us gate (used before 2026-10-06) passed half-captured FSU-on events (missed-packet gaps ~300-375 us at 52 us)
# while rejecting the same misses at 150 us, treating the two arms differently.


def sh(cmd, timeout=180):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def die(msg):
    sys.exit(f'ERROR: {msg}')


def detect():
    out = sh(['nrfutil', 'device', 'list'], timeout=60).stdout
    obs, n54 = [], []
    for blk in re.split(r'\n\s*\n', out):
        lines = blk.strip().splitlines()
        if not lines or not re.match(r'\s*\d{6,}\s*$', lines[0]):
            continue
        kind = 'obs' if 'PCA10040' in blk else ('n54' if 'PCA10156' in blk else None)
        if not kind:
            continue
        want = '0' if kind == 'obs' else '1'
        for ln in lines:
            m = re.search(r'(/dev/tty\S+), vcom: ' + want, ln)
            if m:
                (obs if kind == 'obs' else n54).append((lines[0].strip(), m.group(1).replace('/dev/tty.', '/dev/cu.'))); break
    if len(obs) != 1 or len(n54) != 2:
        die(f'need 1 nRF52 DK + 2 nRF54L15-DK, found {len(obs)} + {len(n54)}; pass --obs/--cen/--per ID:TTY')
    return obs[0], n54[0], n54[1]


def flash(h, sn):
    r = sh(['nrfutil', 'device', 'program', '--firmware', h, '--serial-number', sn])
    if r.returncode: die(f'flash failed {h}: {r.stderr[-300:]}')
    sh(['nrfutil', 'device', 'reset', '--serial-number', sn], timeout=60)   # program leaves the core halted


class Observer:
    def __init__(self, tty, path):
        self.s = serial.Serial(tty, 115200, timeout=0.2); self.s.dtr = True; self.s.rts = True
        self.f = open(path, 'w'); self.lines = []; self.stop = False
        self.t = threading.Thread(target=self._rd, daemon=True); self.t.start()

    def _rd(self):
        buf = b''
        while not self.stop:
            buf += self.s.read(4096)
            while b'\n' in buf:
                ln, buf = buf.split(b'\n', 1)
                txt = ln.decode('utf-8', 'replace').rstrip('\r')
                self.lines.append(txt); self.f.write(txt + '\n'); self.f.flush()

    def wait(self, pat, timeout):
        t_end = time.time() + timeout
        while time.time() < t_end:
            if any(pat in l for l in self.lines): return True
            time.sleep(0.05)
        return False

    def send_slow(self, s):
        for ch in s.encode():
            self.s.write(bytes([ch])); self.s.flush(); time.sleep(0.004)

    def close(self):
        self.stop = True; self.t.join(1); self.s.close(); self.f.close()


def wait_central(path, arm, timeout=30, mode='gatt'):
    """Return (aa, crc, onset_s, spacing) from the central log once its connection and FSU lines exist."""
    t_end = time.time() + timeout
    while time.time() < t_end:
        try: lines = open(path, errors='ignore').read().splitlines()
        except FileNotFoundError: lines = []
        idx = max([i for i, l in enumerate(lines) if 'Booting Zephyr' in l] or [0]); lines = lines[idx:]
        q = [re.search(r'Q2CONN AA=([0-9a-f]{8}) CRCINIT=([0-9a-f]{6})', l) for l in lines]; q = [m for m in q if m]
        done_tag, off_ok = (('Q3FSU-DONE role=C', lambda l: 'Q3FSU-REQ role=C' in l and 'min=150 max=150' in l and 'rc=0' in l)
                            if mode == 'coc' else
                            ('FSU: updated', lambda l: 'FSU: request [150..150]' in l and 'rc=0' in l))
        if arm == 'on':
            f = [l for l in lines if done_tag in l and re.search(r'spacing=(\d+)', l) and int(re.search(r'spacing=(\d+)', l).group(1)) < 150]
        else:
            f = [l for l in lines if off_ok(l)]
        if q and f:
            sp = int(re.search(r'spacing=(\d+)', f[-1]).group(1)) if 'spacing=' in f[-1] else 150
            return q[-1].group(1), q[-1].group(2), sp
        time.sleep(0.2)
    return None


def analyze(obs_path):
    recs = []
    strip = lambda l: re.sub(r'^HOSTMS \d+ ', '', l)          # q2_run logs carry a host-time prefix
    for l in map(strip, open(obs_path, errors='ignore')):
        if l.startswith('REC '):
            d = dict(re.findall(r'(\w+)=(-?\w+)', l))
            try: recs.append((int(d['oseq']), int(d['addr']), int(d['end']), int(d['len']), int(d['crc']), int(d.get('s0', '0'), 16)))
            except (KeyError, ValueError): pass
    recs.sort()
    loss = {}
    for l in map(strip, open(obs_path, errors='ignore')):
        if l.startswith('LOSS:'): loss = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', l))
    events, cur, gaps = [], [], []
    for i, r in enumerate(recs):
        if cur:
            g = ((r[1] - cur[-1][2]) & 0xFFFFFFFF) * TICK_US
            if g > EVENT_SPLIT_US:
                events.append(cur); cur = []
            else:
                cur[-1] = cur[-1] + (g,)    # attach the gap to the previous record
        cur.append(r)
    if cur: events.append(cur)
    complete = []
    allg = [x[6] for ev in events for x in ev[:-1] if len(x) > 6]
    med = statistics.median(allg) if allg else 150.0
    gate = 1.5 * med + MIN_PKT_US / 2
    for ev in events:
        ig = [x[6] for x in ev[:-1] if len(x) > 6]
        if len(ev) >= 2 and all(g <= gate for g in ig):
            complete.append(ev); gaps.extend(ig)
    counts = [len(ev) for ev in complete]
    # A trailing packet that fails CRC was cut off at the event's end (receivers still see the declared
    # length, then a bad CRC): it is never acknowledged, so it is not part of a full exchange.
    full, cut, cdat, pdat, kinds = [], 0, [], [], {'duplex': [], 'one-way': []}
    for ev in complete:
        last_bad = ev[-1][4] != 1
        cut += last_bad
        full.append((len(ev) - (1 if last_bad else 0)) // 2)
        body = ev[:-1] if last_bad else ev                       # roles by alternation: central first
        cdat.append(sum(1 for i, x in enumerate(body) if i % 2 == 0 and x[3] > 0))
        pdat.append(sum(1 for i, x in enumerate(body) if i % 2 == 1 and x[3] > 0))
        # event kind: 'duplex' = both sides send data in every exchange; 'one-way' = one side sends only
        # empty PDUs (an empty+full exchange is ~1.1-1.4 ms vs ~2.2-2.4 ms, so more exchanges fit)
        ce = sum(1 for i, x in enumerate(body) if i % 2 == 0 and x[3] == 0)
        pe = sum(1 for i, x in enumerate(body) if i % 2 == 1 and x[3] == 0)
        if ce == 0 and pe == 0: kinds['duplex'].append(len(body) // 2)
        elif ce >= 0.8 * ((len(body) + 1) // 2) or pe >= 0.8 * (len(body) // 2): kinds['one-way'].append(len(body) // 2)
    res = {'records': len(recs), 'events_seen': len(events), 'events_complete': len(complete), 'gap_median_us': round(med, 1), 'complete_gate_us': round(gate, 1),
           'ring_full_drops': loss.get('ring_full_drops'), 'crc_bad': sum(1 for r in recs if r[4] != 1)}
    if counts:
        mode = max(set(counts), key=counts.count)
        res.update({'pkts_mode': mode, 'pkts_mode_share': round(counts.count(mode) / len(counts), 3),
                    'pkts_mean': round(statistics.mean(counts), 2),
                    'full_exch_mode': max(set(full), key=full.count),
                    'full_exch_mode_share': round(full.count(max(set(full), key=full.count)) / len(full), 3),
                    'trailing_cut_share': round(cut / len(complete), 3),
                    'central_data_mean': round(statistics.mean(cdat), 2), 'periph_data_mean': round(statistics.mean(pdat), 2),
                    'duplex_events': len(kinds['duplex']), 'oneway_events': len(kinds['one-way']),
                    'duplex_exch_mode': statistics.multimode(kinds['duplex']) if kinds['duplex'] else [],
                    'oneway_exch_mode': statistics.multimode(kinds['one-way']) if kinds['one-way'] else [],
                    'gap_us_median': round(statistics.median(gaps), 2) if gaps else None,
                    'tifs_us_est': round(statistics.median(gaps) - 24, 1) if gaps else None,
                    'data_pkt_share': round(sum(1 for ev in complete for r in ev if r[3] > 0) / sum(counts), 3)})
    return res


def print_summary(results, intervals):
    print('\n=== ON-AIR SUMMARY (accepted reps) ===')
    for u in intervals:
        for arm in ('off', 'on'):
            rs = [r for r in results if r.get('interval_units') == u and r.get('arm') == arm and not r['reject']]
            if rs:
                print(f'{u*1.25:>5} ms FSU-{arm:<3}: packets/event {sorted({r["pkts_mode"] for r in rs})}  '
                      f'full exchanges/event {sorted({r["full_exch_mode"] for r in rs})}  '
                      f'trailing cut-off {statistics.mean(r["trailing_cut_share"] for r in rs):.0%}  '
                      f'data pkts/event C {statistics.mean(r.get("central_data_mean", 0) for r in rs):.1f} '
                      f'P {statistics.mean(r.get("periph_data_mean", 0) for r in rs):.1f}  '
                      f'in-event gap {statistics.median(r["gap_us_median"] for r in rs):.1f} us '
                      f'(tIFS ~{statistics.median(r["tifs_us_est"] for r in rs):.0f} us)  '
                      f'complete events {sum(r["events_complete"] for r in rs)}  reps {len(rs)}')
                if any(r.get('oneway_events') for r in rs):
                    dx = sum(r.get('duplex_events', 0) for r in rs); ow = sum(r.get('oneway_events', 0) for r in rs)
                    print(f'{"":>18}event kinds: duplex {dx} (exchanges mode {sorted({m for r in rs for m in r.get("duplex_exch_mode", [])})})  '
                          f'one-way {ow} = {ow / max(1, dx + ow):.0%} (exchanges mode {sorted({m for r in rs for m in r.get("oneway_exch_mode", [])})})')


def reanalyze(out_dirs, intervals):
    results = []
    for d in out_dirs:
        for l in open(os.path.join(d, 'results.jsonl')):
            r = json.loads(l)
            if not r['reject']:
                r.update(analyze(os.path.join(d, 'caps', f"{r['tag']}-obs.log")))
            results.append(r)
    print_summary(results, intervals)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--builds'); ap.add_argument('--obs-build'); ap.add_argument('--out')
    ap.add_argument('--intervals', default='6,12,20')
    ap.add_argument('--reanalyze', nargs='+', metavar='OUT_DIR', help='recompute from archived run dirs, no hardware')
    ap.add_argument('--mode', choices=['gatt', 'coc'], default='gatt')
    ap.add_argument('--obs'); ap.add_argument('--cen'); ap.add_argument('--per')
    a = ap.parse_args()
    intervals = [int(x) for x in a.intervals.split(',')]
    if a.reanalyze:
        reanalyze(a.reanalyze, intervals); return
    if a.obs and a.cen and a.per:
        (o, otty), (c, ctty), (p, ptty) = [x.split(':', 1) for x in (a.obs, a.cen, a.per)]
    else:
        (o, otty), (c, ctty), (p, ptty) = detect()
    print(f'observer {o} {otty} / central {c} {ctty} / peripheral {p} {ptty}', flush=True)
    caps = os.path.join(a.out, 'caps'); os.makedirs(caps, exist_ok=True)
    flash(os.path.join(a.obs_build, 'zephyr', 'zephyr.hex'), o)
    flash(os.path.join(a.builds, 'p-echo' if a.mode == 'gatt' else 'sink', 'zephyr', 'zephyr.hex'), p)
    results = []
    for u in intervals:
        loaded = None
        for i, arm in enumerate(ORDER):
            tag = f'onair{"coc" if a.mode == "coc" else ""}{u}_{arm}_r{i+1}'
            if arm != loaded:
                flash(os.path.join(a.builds, f'c-{arm}-{u}', 'zephyr', 'zephyr.hex'), c); loaded = arm
                if a.mode == 'coc': time.sleep(15)          # discard the first (wedge-prone) connection
            sh(['nrfutil', 'device', 'reset', '--serial-number', o], timeout=60)
            obs = Observer(otty, f'{caps}/{tag}-obs.log')
            if not obs.wait('CONFIG-READY', 10):
                sh(['nrfutil', 'device', 'reset', '--serial-number', o], timeout=60)
                if not obs.wait('CONFIG-READY', 10):
                    obs.close(); results.append({'tag': tag, 'reject': ['observer not ready']}); continue
            r = {'tag': tag, 'arm': arm, 'interval_units': u, 'mode': a.mode, 'reject': []}
            # The central's deferred log buffer can overflow at connection time and drop the Q2CONN line
            # ("--- 1 messages dropped ---", ~2% of boots). Reconnect for a fresh access address rather
            # than reject; the failed attempt's logs are kept as <tag>-tryN-*.log.
            for attempt in range(1, CONN_ATTEMPTS + 1):
                cap = subprocess.Popen([sys.executable, os.path.join(REPO, 'tools', 'capture-tool.py'), '60',
                                        f'{ctty}:{tag}-cen', f'{ptty}:{tag}-per'],
                                       env=dict(os.environ, CAP_OUTDIR=caps), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(1.5)
                sh(['nrfutil', 'device', 'reset', '--serial-number', p], timeout=60)
                if a.mode == 'gatt':
                    sh(['nrfutil', 'device', 'reset', '--serial-number', c], timeout=60)
                got = wait_central(f'{caps}/{tag}-cen.log', arm, mode=a.mode)
                if got or attempt == CONN_ATTEMPTS:
                    break
                cap.terminate(); cap.wait(timeout=30)
                log = open(f'{caps}/{tag}-cen.log', errors='ignore').read()
                why = 'log buffer dropped it' if 'messages dropped' in log else 'not printed'
                r.setdefault('retries', []).append(f'attempt {attempt}: no Q2CONN ({why})')
                for side in ('cen', 'per'):
                    if os.path.exists(f'{caps}/{tag}-{side}.log'):
                        os.replace(f'{caps}/{tag}-{side}.log', f'{caps}/{tag}-try{attempt}-{side}.log')
            if not got:
                r['reject'].append(f'no Q2CONN/FSU line from the central after {CONN_ATTEMPTS} connections')
            else:
                aa, crc, sp = got; r.update({'aa': aa, 'crc': crc, 'spacing': sp})
                time.sleep(3.0)              # let FSU settle before capturing
                obs.send_slow(f'CFG aa=0x{aa} crc=0x{crc} ch=10 phy=2\n')
                if not obs.wait('ARMED', 10):
                    r['reject'].append('observer not armed')
                else:
                    obs.s.write(b'G'); obs.s.flush()
                    if not obs.wait('Q2-DONE', 90):
                        r['reject'].append('observer dump incomplete')
            obs.close(); cap.wait(timeout=120)
            if not r['reject']:
                r.update(analyze(f'{caps}/{tag}-obs.log'))
                if r.get('events_complete', 0) < 5: r['reject'].append(f"only {r.get('events_complete', 0)} complete events")
                if r.get('ring_full_drops'): r['reject'].append(f"ring_full_drops={r['ring_full_drops']}")
            results.append(r)
            print(json.dumps({k: r.get(k) for k in ('tag', 'spacing', 'events_complete', 'pkts_mode', 'full_exch_mode',
                                                     'trailing_cut_share', 'gap_us_median', 'tifs_us_est', 'retries', 'reject') if k in r}), flush=True)
            with open(os.path.join(a.out, 'results.jsonl'), 'a') as f: f.write(json.dumps(r) + '\n')
    print_summary(results, intervals)
    sys.exit(1 if any(r['reject'] for r in results) else 0)


if __name__ == '__main__':
    main()
