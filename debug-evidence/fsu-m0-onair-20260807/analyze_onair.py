#!/usr/bin/env python3
"""On-air FSU capture analyzer (rev 3, hardened per review). Selects the
target connection from the target's CONNECT_IND (never by frequency),
rejects mixed/non-monotonic captures, ambiguous/duplicate CONNECT_INDs,
empty packet lists, and tshark errors; requires the connection to span the
FSU procedure AND (for an accepted run) both an HCI completion and a
captured LL_FRAME_SPACE_RSP. Exits nonzero on any rejection/incomplete.

Usage:  analyze_onair.py <pcap> <target_addr> [central_log] [--spacing N]
        analyze_onair.py --selftest <any_pcap>
tIFS = CRC-good, consecutive, OPPOSITE-direction packets in the same event.
Exit: 0 accept · 2 reject (malformed/does-not-span) · 3 incomplete capture.
"""
import subprocess, sys, collections

def tsh(pcap, disp, fs):
    cmd = ['tshark','-r',pcap]
    if disp: cmd += ['-Y',disp]
    cmd += ['-T','fields']
    for f in fs: cmd += ['-e',f]
    r = subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode != 0:
        sys.stderr.write(f'tshark error ({" ".join(fs)}): {r.stderr.strip()}\n')
        sys.exit(2)
    return r.stdout.splitlines()

def analyze(pcap, target, clog, spacing):
    target = target.lower()
    # gate 1: strict timestamp monotonicity
    times = [float(x) for x in tsh(pcap,None,['frame.time_relative']) if x]
    if not times:
        print('REJECT: empty capture.'); return 2
    nonmono = sum(1 for a,b in zip(times,times[1:]) if b < a - 1e-6)
    if nonmono:
        print(f'REJECT: non-monotonic capture ({nonmono} backward jumps) -> '
              f'mixed/appended pcap.'); return 2
    # gate 2: exactly one CONNECT_IND to the target -> its AA
    ci = tsh(pcap,'btle.advertising_header.pdu_type==0x05',
             ['btle.advertising_address','btle.link_layer_data.access_address'])
    hits = [p[1] for ln in ci if len(p:=ln.split('\t'))==2 and p[0].lower()==target and p[1]]
    if len(hits) == 0:
        print(f'REJECT: no CONNECT_IND to target {target}.'); return 2
    if len(set(hits)) > 1:
        print(f'REJECT: multiple distinct CONNECT_IND AAs to {target}: {set(hits)} '
              f'-> ambiguous target connection.'); return 2
    caa = hits[0]
    print(f'target connection AA = {caa} (CONNECT_IND to {target})')
    rows = tsh(pcap,f'btle.access_address=={caa}',
        ['frame.time_relative','nordic_ble.direction','nordic_ble.event_counter',
         'btle.control_opcode','nordic_ble.delta_time','nordic_ble.crcok',
         'btle.control.instant'])
    P = []
    for ln in rows:
        f = (ln.split('\t') + ['']*7)[:7]
        if not f[0]: continue
        P.append(dict(t=float(f[0]), dir=f[1], ev=int(f[2]) if f[2].isdigit() else None,
                      op=f[3], dt=int(f[4]) if f[4].isdigit() else None,
                      crc=(f[5] in ('1','True')), inst=int(f[6]) if f[6].isdigit() else None))
    if not P:
        print('REJECT: target AA yielded no packets.'); return 2
    print(f'target packets: {len(P)}  span {P[0]["t"]:.3f}->{P[-1]["t"]:.3f}s')
    def find(op): return [p for p in P if op in (p['op'] or '')]
    req, rsp, upd = find('0x3b'), find('0x3c'), find('0x00')
    print(f'CHRONOLOGY: REQ={[(round(p["t"],3),p["ev"]) for p in req] or None} '
          f'RSP={[(round(p["t"],3),p["ev"]) for p in rsp] or None} '
          f'UPDATE={[(round(p["t"],3),p["ev"],p["inst"]) for p in upd] or None}')
    hci = bool(clog and (f'FSU: updated status=0x00 spacing={spacing}'
                         in open(clog,errors='replace').read()))
    print(f'central HCI FSU completion (spacing={spacing}): {hci}')
    # gate 3: must span the FSU procedure
    if not req:
        print('REJECT: target connection contains NO FSU_REQ (does not span FSU).'); return 2

    # DIAGNOSTICS: computed BEFORE the accept/incomplete gates so the numbers
    # are reproduced even when the run is INCOMPLETE. Window boundary = the
    # FSU_REQ event; end = the connection-update INSTANT if present.
    import collections as _c
    end_ev = upd[0]['inst'] if (upd and upd[0]['inst']) else None
    req_ev = req[0]['ev']
    def phase(p):
        if p['ev'] is None: return '?'
        if p['ev'] < req_ev: return 'before-REQ'
        if end_ev is not None and p['ev'] >= end_ev: return 'after-INSTANT'
        return 'after-REQ'
    def tifs(ph):
        out=[b['dt'] for a,b in zip(P,P[1:])
             if phase(b)==ph and a['crc'] and b['crc'] and a['ev']==b['ev']
             and a['ev'] is not None and a['dir'] and b['dir'] and a['dir']!=b['dir']
             and b['dt'] and 30<=b['dt']<=300]
        return sorted(out)
    print('DIAGNOSTICS per phase (packets, reported-direction counts, paired '
          'CRC-good opposite-dir same-event tIFS):')
    for ph in ('before-REQ','after-REQ','after-INSTANT'):
        seg=[p for p in P if phase(p)==ph]
        if not seg: continue
        dc=_c.Counter(p['dir'] for p in seg)
        g=tifs(ph)
        print(f'  {ph:13s}: pkts={len(seg)} dirs={dict(dc)} | '
              f'tIFS n={len(g)} median={g[len(g)//2] if g else "-"}'
              + (f' [{g[0]},{g[-1]}]' if g else ''))
    print('NOTE: absence of reduced-spacing gaps is informative ONLY with a '
          'positive control; a one-direction phase = spacing UNMEASURABLE here.')

    # accept/incomplete/reject gates (AFTER diagnostics)
    if not clog or not hci:
        print('REJECT: no HCI FSU completion for the expected spacing.'); return 2
    if not rsp:
        print('INCOMPLETE: HCI completion but NO LL_FRAME_SPACE_RSP captured '
              '-> sniffer missed the decisive response.'); return 3
    print('ACCEPT: complete capture spanning REQ+RSP with HCI completion.')
    return 0

def selftest(pcap):
    # extract a monotonic prefix (frames before any backward jump) and confirm
    # the analyzer REJECTS cleanly (no crash) rather than erroring on fields.
    import tempfile, os
    times = tsh(pcap,None,['frame.number','frame.time_relative'])
    cut = None; prev = -1
    for ln in times:
        p = ln.split('\t')
        if len(p)==2 and p[1]:
            t=float(p[1])
            if t < prev - 1e-6: cut=int(p[0])-1; break
            prev=t
    seg = tempfile.mktemp(suffix='.pcap')
    rng = f'1-{cut}' if cut else '1-100'
    subprocess.run(['editcap','-r',pcap,seg,rng],capture_output=True)
    print(f'SELFTEST: monotonic segment frames {rng} -> analyze (expect clean REJECT):')
    rc = analyze(seg,'d6:c7:60:b1:d3:0e',None,52)
    os.unlink(seg)
    ok = rc in (2,3)   # a clean rejection, NOT a crash/exception
    print(f'SELFTEST: {"PASS (clean rejection rc=%d)"%rc if ok else "FAIL rc=%d"%rc}')
    return 0 if ok else 1

if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--selftest':
        sys.exit(selftest(a[1]))
    spacing = 52
    if '--spacing' in a:
        i=a.index('--spacing'); spacing=int(a[i+1]); del a[i:i+2]
    pcap=a[0]; target=a[1]; clog=a[2] if len(a)>2 else None
    sys.exit(analyze(pcap,target,clog,spacing))
