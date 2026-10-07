#!/usr/bin/env python3
"""
analyze.py — turnkey parser for the capture logs this repo's rigs produce.

Usage:  python3 analyze.py <log> [<log> ...]
        python3 analyze.py scratch/*.log

Auto-detects the line format and prints the headline number(s) per log:
  - CoC throughput      (sink:  "SINK rx: ... cum_total=<B> cum_segs=<S>")   -> KB/s from cum slope
  - GATT-write tput     (sink:  "GATT-WRITE rx: <k> KB/s")                    -> median KB/s
  - GATT notify tput    (periph:"P t=<s>s rxkBps=<n> blkkBps=<n>")            -> median KB/s
  - CoC uplink (duplex) (cen:   "CENRX cum_total=<B>")                        -> KB/s from slope
  - Latency-under-load  (cen:   "LAT[2]: ... rtt_us[min/mean/max]=a/b/c over30ms=o")
  - z54-lat RTT ramp    (cen:   "t=Ns RTT mean=<us> ..." + "LOADRAMP: target=<KBps>")
Also prints sanity flags: the FINAL connection interval (flagged if it changed after connect), PHY, DLE, FSU spacing.
Rates use only the last connection in a log (cumulative counters restart on reconnect).
Steady window = drop the first 25% of samples; throughput from endpoint cum-slope.
Reproduce DELTAS (FSU on/off, transport-vs-transport), not absolute KB/s.
"""
import sys, re
from statistics import median, mean

def steady(xs):
    return xs[len(xs)//4:] if len(xs) > 8 else xs

def _ms(v):
    """interval value -> ms: the apps print 1.25 ms units (small) or microseconds (large)."""
    return v * 1.25 if v < 1000 else v / 1000.0

def sanity(text):
    """Report the FINAL negotiated values (a link starts at 1M / 27-octet PDUs and updates; the first value is not
    the operating point) and flag an interval that changed after connect."""
    out = []
    iv = [_ms(int(m.group(1))) for m in re.finditer(r'(?:GATE conn: interval=|GAP connected:? interval=|>>> interval now )(\d+)', text)]
    if iv:
        out.append(f"interval={iv[-1]:g}ms" + (f"(!changed: {sorted(set(iv))})" if len(set(iv)) > 1 else ""))
    phy = re.findall(r'PHY(?: updated)?:? tx=(\d) rx=(\d)', text)
    if phy: out.append("PHY=2M" if phy[-1] == ('2', '2') else f"PHY=tx{phy[-1][0]}/rx{phy[-1][1]}(!)")
    dle = re.findall(r'DLE(?: updated)?:? tx_max=(\d+) rx_max=(\d+)', text)
    if dle: out.append(f"DLE={dle[-1][0]}/{dle[-1][1]}")
    sp = sorted(set(re.findall(r'(?:spacing|fsu)=(\d+)', text)), key=int)
    if sp: out.append("fsu=" + "/".join(sp))
    return "  ".join(out) or "(no handshake in window)"

def last_connection(rows):
    """Cumulative counters restart at each connection: keep only the rows after the last restart, so a log that
    holds several connections is not turned into one wrong slope."""
    k = 0
    for i in range(1, len(rows)):
        if rows[i][1] < rows[i - 1][1]: k = i
    return rows[k:]

def coc_tput(rows):  # rows: (t, cum_bytes, cum_segs)
    rows = last_connection([r for r in rows if r[1] > 0])
    if len(rows) < 3: return None
    r = steady(rows)
    (t0, c0, s0), (t1, c1, s1) = r[0], r[-1]
    dt = t1 - t0
    if dt <= 0: return None
    return {"KB/s": (c1-c0)/dt/1024, "seg/s": (s1-s0)/dt}

def analyze(path):
    try: text = open(path).read()
    except OSError as e: return f"{path}: {e}"
    L = text.splitlines()
    head = f"\n=== {path.split('/')[-1]} ===\n  sanity: {sanity(text)}"

    # latency (LAT / LAT2)
    lat = [(int(m[1]), int(m[2])) for ln in L for m in [re.search(r'rtt_us\[min/mean/max\]=\d+/(\d+)/(\d+)', ln)] if m and 'pings=0' not in ln]
    ov = [int(m[1]) for ln in L for m in [re.search(r'over30ms=(\d+)', ln)] if m]
    npg = [int(m[1]) for ln in L for m in [re.search(r'pings=(\d+)', ln)] if m]
    if lat:
        means = steady([a for a, _ in lat]); maxs = [b for _, b in lat]
        ovpct = 100*sum(steady(ov))/max(1, sum(steady(npg))) if npg else 0
        return head + f"\n  LATENCY: rtt mean={median(means)/1000:.1f}ms  max={max(maxs)/1000:.1f}ms  >30ms={ovpct:.0f}%"

    # z54-lat RTT ramp (LOADRAMP stages)
    if 'LOADRAMP: target=' in text:
        stage=0; st={}
        for ln in L:
            m=re.search(r'LOADRAMP: target=(\d+)', ln)
            if m: stage=int(m[1]); continue
            r=re.search(r'RTT mean=(\d+).*n=(\d+)', ln)
            if r and int(r[2])>0: st.setdefault(stage, []).append(int(r[1]))
        s = head + "\n  GATT RTT vs offered load:"
        for k in sorted(st): s += f"\n    {k:>4} KB/s -> {median(steady(st[k]))/1000:.1f}ms"
        return s

    # CoC sink throughput
    rows = [(float(m[1]), int(m[2]), int(m[3])) for ln in L for m in [re.search(r'^\s*([\d.]+)\s+SINK rx:.*cum_total=(\d+) B cum_segs=(\d+)', ln)] if m]
    if rows:
        r = coc_tput(rows)
        extra = ""
        up = [(float(m[1]), int(m[2])) for ln in open(path) for m in [re.search(r'^\s*([\d.]+)\s+CENRX cum_total=(\d+)', ln)] if m]  # rarely same file
        return head + (f"\n  CoC throughput: {r['KB/s']:.1f} KB/s ({r['seg/s']:.0f} seg/s)" if r else "\n  (no steady CoC data)")

    # CoC uplink / duplex (central CENRX)
    cen = [(float(m[1]), int(m[2])) for ln in L for m in [re.search(r'^\s*([\d.]+)\s+CENRX cum_total=(\d+)', ln)] if m]
    if cen:
        cen=last_connection([c for c in cen if c[1]>0]); r=steady(cen); (t0,c0),(t1,c1)=r[0],r[-1]
        return head + f"\n  CoC uplink (CENRX): {(c1-c0)/(t1-t0)/1024:.1f} KB/s"

    # GATT-write sink
    g = [int(m[1]) for ln in L for m in [re.search(r'GATT-WRITE rx: (\d+) KB/s', ln)] if m]
    if g: return head + f"\n  GATT-write throughput: {median(steady(g)):.0f} KB/s"

    # GATT notify periph
    p = [int(m[1]) for ln in L for m in [re.search(r'rxkBps=(\d+)', ln)] if m]
    if p: return head + f"\n  GATT rxkBps: median {median(steady(p)):.0f} KB/s"

    return head + "\n  (no recognized throughput/latency lines)"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    for p in sys.argv[1:]:
        print(analyze(p))
