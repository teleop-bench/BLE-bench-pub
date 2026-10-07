#!/usr/bin/env python3
"""verify_run.py — per-capture benchmark gate. Turns silent-wrong-data into a loud REJECT.

Every error this project hit was a SILENT one. This asserts the invariants from the capture log
(+ optional resolved .config), so a run that violates them is rejected instead of producing a
plausible-but-wrong number. THE central check: FSU must be held THROUGHOUT the run, not just
negotiated once — verified from the throughput time-series (the `spacing=52` token is LATCHED/STALE
and keeps printing after a revert, so it is NOT a valid liveness signal; the throughput plateau is).

Usage:
  python3 tools/verify_run.py <per.log> [--cen <cen.log>] --fsu on|off [--transport CoC|GATT]
     [--interval-ms 15] [--sdu 480] [--min-kbps 60] [--expect-version v4.4.2]
Exit 0 = PASS, 1 = REJECT. Prints a verdict block; import verify(...) for the dict.
"""
import sys, re, json, argparse, statistics

def parse_series(path):
    """(t_seconds, kbps) from a sink/peripheral log — CoC 'SINK rx: N KB/s' or GATT 'P t=Ns rxkBps=N'."""
    s = []
    try:
        for l in open(path):
            m = re.search(r'^\s*([0-9.]+)\s+SINK rx:\s*([0-9]+) KB/s', l) or \
                re.search(r'^\s*([0-9.]+)\s+P t=\d+s rxkBps=([0-9]+)', l)
            if m:
                s.append((float(m.group(1)), int(m.group(2))))
    except Exception:
        pass
    return s

def parse_facts(*paths):
    """achieved operating point + version + spacing, scanned from cen/per logs."""
    f = {'version': None, 'phy': None, 'dle': None, 'interval_ms': None, 'spacing_last': None,
         'spacing_seen': set(), 'stack': None}
    for p in paths:
        if not p:
            continue
        try:
            txt = open(p).read()
        except Exception:
            continue
        m = re.search(r'Booting Zephyr OS build (\S+)', txt);          f['version'] = f['version'] or (m and m.group(1))
        if re.search(r'SoftDevice', txt):                              f['stack'] = 'SDC'
        m = re.search(r'phy[=\s]*(?:tx=)?([12])', txt, re.I);          f['phy'] = f['phy'] or (m and m.group(1))
        m = re.search(r'(?:DLE|tx_max|data.?len)\D*(\d{2,3})', txt, re.I); f['dle'] = f['dle'] or (m and m.group(1))
        if f['interval_ms'] is None:
            # interval is printed two ways: "interval=N us" (microseconds) OR "interval=N" in 1.25ms UNITS
            # (e.g. GATE conn: interval=12 = 15 ms). Normalize both to ms; units are small (<1000), us are large.
            mm = re.search(r'interval=(\d+)\s*us', txt, re.I)
            if mm:
                f['interval_ms'] = int(mm.group(1)) / 1000.0
            else:
                mm = re.search(r'interval=(\d+)', txt, re.I)
                if mm:
                    v = int(mm.group(1)); f['interval_ms'] = v * 1.25 if v < 1000 else v / 1000.0
        for sm in re.finditer(r'spacing=(\d+)', txt):
            v = int(sm.group(1)); f['spacing_seen'].add(v); f['spacing_last'] = v
    f['spacing_seen'] = sorted(f['spacing_seen'])
    return f

def fsu_held(series, settle=3.0, revert_drop=0.08, min_run=3):
    """FSU held THROUGHOUT? Returns (held: bool, reason, detail).
    Two independent revert detectors over the post-settle steady region:
      (A) net: last-third median < first-third median * (1-revert_drop)  -> net step-down
      (B) sustained: a run of >=min_run consecutive seconds >revert_drop below the running peak plateau."""
    steady = [(t, k) for t, k in series if t >= settle and k > 0]
    if len(steady) < 6:
        return (False, 'too-short', {'n': len(steady)})
    ks = [k for _, k in steady]
    n = len(ks)
    third = max(2, n // 3)
    first = statistics.median(ks[:third]); last = statistics.median(ks[-third:])
    netA = last < first * (1 - revert_drop)
    # sustained-drop scan (throughout) — a REVERT is a drop AFTER reaching the plateau, not the ramp-UP
    # TO it. So find where the series first reaches the peak plateau, and only scan seconds after that.
    peak = statistics.median(sorted(ks, reverse=True)[:max(2, n // 4)])
    thresh = peak * (1 - revert_drop)
    reached = next((i for i, k in enumerate(ks) if k >= thresh), len(ks))
    post = ks[reached:]                 # only post-plateau: excludes the initial ramp-up
    # A revert is irreversible within a connection (FSU is requested once at setup), so a drop only
    # counts if the plateau never comes back. A dip that recovers for >=min_run seconds is a transient
    # (RF/retransmit), reported but not rejected.
    run = mx = 0; sustB = False; transient = 0; i = 0
    while i < len(post):
        if post[i] < thresh:
            j = i
            while j < len(post) and post[j] < thresh: j += 1
            run = j - i; mx = max(mx, run)
            if run >= min_run:
                rest = post[j:]; best = cur = 0
                for k in rest:
                    cur = cur + 1 if k >= thresh else 0; best = max(best, cur)
                if best >= min_run: transient += 1
                else: sustB = True
            i = j
        else:
            i += 1
    held = not (netA or sustB)
    return (held, 'held' if held else ('net-stepdown' if netA else 'sustained-drop'),
            {'first_third': round(first, 1), 'last_third': round(last, 1), 'peak': round(peak, 1),
             'max_below_run_s': mx, 'transient_dips': transient})

def parse_uplink_slope(cen_log):
    """duplex uplink KB/s = slope of the central's 'CENRX cum_total=N B' counter over the steady tail."""
    pts = []
    try:
        for l in open(cen_log):
            m = re.search(r'^\s*([0-9.]+)\s+CENRX cum_total=(\d+)', l)
            if m: pts.append((float(m.group(1)), int(m.group(2))))
    except Exception:
        pass
    if len(pts) < 3: return 0.0
    k = len(pts) // 3                                   # steady tail (skip ramp)
    dt = pts[-1][0] - pts[k][0]; db = pts[-1][1] - pts[k][1]
    return (db / dt / 1024) if dt > 0 else 0.0

def verify_duplex(per_log, cen_log, fsu='on', interval_ms=None, stack='open'):
    """Duplex gate: BOTH directions must be alive (neither stalled = the reconnect wedge) AND held.
    downlink = SINK rx KB/s (per); uplink = CENRX slope (cen). Rejects a wedge-stalled or reverted rep."""
    dl_series = parse_series(per_log)
    dl_steady = [k for t, k in dl_series if t >= 3 and k > 0]
    dl = statistics.median(dl_steady) if dl_steady else 0.0
    ul = parse_uplink_slope(cen_log)
    r = {'pass': True, 'rejects': [], 'warns': [], 'downlink': round(dl, 1), 'uplink': round(ul, 1),
         'aggregate': round(dl + ul, 1), 'fsu': fsu}
    def rej(m): r['pass'] = False; r['rejects'].append(m)
    if dl < 5: rej(f"downlink stalled/dead ({dl:.0f} KB/s)")
    if ul < 5: rej(f"UPLINK STALLED ({ul:.0f} KB/s) — reconnect wedge; EXCLUDE this rep + count the stall")
    if fsu == 'on' and len(dl_series) >= 6:
        held, why, detail = fsu_held(dl_series)         # revert shows as a downlink step-down
        r['fsu_held_downlink'] = held
        if not held: rej(f"downlink FSU not held ({why}): {detail}")
    return r

def verify(per_log, cen_log=None, fsu='on', transport=None, interval_ms=None, sdu=None,
           min_kbps=5, expect_version=None, stack='open'):
    # NOTE: min_kbps is a DEAD-LINK floor only (is data flowing at all?), NOT a benchmark threshold.
    # Absolute throughput is RF/distance-dependent and must NEVER gate a run — only structural/relative
    # invariants do (FSU-held is relative to the run's own peak; operating-point/version/config are static).
    r = {'pass': True, 'rejects': [], 'warns': [], 'facts': {}}
    series = parse_series(per_log)
    facts = parse_facts(per_log, cen_log)
    r['facts'] = facts
    steady = [k for t, k in series if t >= 3.0 and k > 0]
    r['steady_kbps'] = round(statistics.median(steady), 1) if steady else 0.0
    def rej(m): r['pass'] = False; r['rejects'].append(m)

    # (0) link alive / not ramp-only
    if not steady or r['steady_kbps'] < min_kbps:
        rej(f"dead-or-low link: steady {r['steady_kbps']} KB/s < {min_kbps}")
    # (1) THE central check — FSU held throughout (only on the FSU-on arm)
    if fsu == 'on':
        held, why, detail = fsu_held(series)
        r['fsu_held'] = held; r['fsu_held_detail'] = detail
        if not held:
            rej(f"FSU did NOT hold throughout ({why}): {detail} — likely mid-run revert "
                f"(check responder BT_GAP_AUTO_UPDATE_CONN_PARAMS=n)")
        # spacing negotiated (necessary, not sufficient — token is latched). Only the OPEN controller
        # emits a 52 token; SDC uses its own lowest frame space (65/70 us) and emits no app token, so on
        # SDC the held-throughput check (above) + a positive on/off delta are the evidence, not a token.
        is_sdc = (stack == 'sdc') or (facts['stack'] == 'SDC')
        if not is_sdc and 52 not in facts['spacing_seen']:
            rej(f"FSU-on (open) but spacing=52 never seen (spacing_seen={facts['spacing_seen']}) — "
                f"floor clamped? verify CONN_INTERVAL_LOW_LATENCY on BOTH ends")
    # (2) operating point matches intent
    if facts['phy'] and facts['phy'] != '2':
        r['warns'].append(f"PHY!=2M (phy={facts['phy']})")
    if interval_ms is not None and facts['interval_ms']:
        if abs(facts['interval_ms'] - interval_ms) > 0.3:
            rej(f"interval mismatch: achieved {facts['interval_ms']}ms != declared {interval_ms}ms")
    # (3) version stamp / expectation
    if expect_version and facts['version'] and expect_version not in facts['version']:
        r['warns'].append(f"version {facts['version']} != expected {expect_version}")
    return r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('per_log'); ap.add_argument('--cen'); ap.add_argument('--fsu', default='on', choices=['on', 'off'])
    ap.add_argument('--transport'); ap.add_argument('--interval-ms', type=float); ap.add_argument('--sdu', type=int)
    ap.add_argument('--min-kbps', type=float, default=5); ap.add_argument('--expect-version')
    ap.add_argument('--stack', default='open', choices=['open', 'sdc'])
    a = ap.parse_args()
    r = verify(a.per_log, a.cen, a.fsu, a.transport, a.interval_ms, a.sdu, a.min_kbps, a.expect_version, a.stack)
    verdict = 'PASS' if r['pass'] else 'REJECT'
    print(f"[{verdict}] {a.per_log}  steady={r.get('steady_kbps')}KB/s  version={r['facts'].get('version')}  "
          f"spacing_seen={r['facts'].get('spacing_seen')}  fsu_held={r.get('fsu_held','n/a')}")
    for m in r['rejects']: print("  REJECT:", m)
    for m in r['warns']:   print("  warn:  ", m)
    sys.exit(0 if r['pass'] else 1)

if __name__ == '__main__':
    main()
