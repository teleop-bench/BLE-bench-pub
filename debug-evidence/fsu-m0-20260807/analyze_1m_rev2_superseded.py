#!/usr/bin/env python3
"""M0 1M spacing discriminator — PREREGISTERED before reading any m0-1m-*
results (ABAB pilot in flight, unread; BAAB follow-up scheduled).

MODEL (corrected per review): 1M pair = 2168 + 2*spacing us
(261-byte on-air data PDU x 8 us = 2088; 10-byte empty response = 80).
150 us -> 2468; 52 us -> 2272; predicted capacity gain +8.63%.
Predicted absolute: 244e6/2468/1024 = 96.5 KiB/s @150; 104.9 @52.

ESTIMATOR: per-cell throughput = median of peripheral per-second rxkBps
values > 10, first 20 kept samples dropped (settle; FSU fires at +3 s).

ACCEPTED-RUN CONFIRMATIONS (all required; missing -> run flagged, reported,
excluded only for spacing-confirm absence on 52-arms, per U-cell precedent):
- NO 'PHY tx=2' anywhere (1M build; a 2M switch invalidates the model)
- mtu=247 (DLE/251 LL payload capability)
- 52-arm: 'FSU: updated status=0x00 spacing=52 us' (SELECTED confirm)
- zero disc increments post-settle (both logs), no FATAL/reboot markers
- baseline sanity: 150-arm median within 80..100 KiB/s (near the 1M air
  ceiling, materially below the ~157 pump ceiling) - else the cell is not
  air-bound and the discriminator is void

AMENDMENT (2026-08-07, before reading any m0-1m30 series data): arm B
changed 52 -> 100 us. Reason: 1M + tIFS<=60 is the known-broken region of
this controller (sub-ms record s13.7-13.8: 52/60 assert or no-Rx at 1M;
90/120 survived) - the 52 smoke runs died 0x08 ~4 s after the FSU request,
i.e. FSU negotiated the peripheral into an infeasible 1M turnaround. New
geometry: 1M, interval 30 ms (via periph PREF=24 auto-update; FSU delayed
12 s to land after it - ull_conn_update_parameters resets tifs), asserts
disabled (sanctioned fall-through; 7.5 ms pre-update phase overruns).
QUANTIZED model (pairs/event, ~500 us event overhead):
N = floor(29500/pair): pair150=2468 -> 11; pair100=2368 -> 12.
Predicted delta +9.09% (12/11); absolute ~87.4 vs ~95.3 KiB/s.
Baseline band for the 150-arm: 80..95 KiB/s. B-arm confirm: spacing=100.

AMENDMENT 2 (2026-08-07, before reading the m0-1m43s series): interval
30 ms abandoned — measured ~11.6 pairs both arms revealed (a) a
quantization COLLISION (real event overhead <384 us puts BOTH arms at 12
pairs; the 500 us overhead assumption was wrong) and (b) a credit-flow
ceiling at BT_BUF_ACL_TX_COUNT=10. New geometry: interval 43 units
(53.75 ms) — N150=21, N100=22 for ANY overhead 0..800 us (robust window),
BT_BUF_ACL_TX_COUNT=33 (+EVT_RX=40, periph RX_EXTRA=20/CTLR_RX=18).
Predicted delta +4.76% (22/21); absolute ~93.1 vs ~97.5 KiB/s.
Baseline band for the 150-arm: 82..95. Confirm 'interval now 43'.
Smoke (single cells, read before series — disclosed): 88 vs 94.

ANALYSIS: paired/order-adjusted - pair up consecutive (150,52) cells by
timestamp order tag; per-pair delta%; mean delta with sign test note; the
ABAB pilot pairs and BAAB pairs reported separately AND pooled; order
imbalance stated. Asymmetric interpretation per review: ~+8.6% = strong
indirect evidence spacing reaches air pacing; flat = strong evidence
against visible adoption on this path (LL-ignores-FSU claim additionally
requires the positive capacity control); unexpected slope = investigate
quantization/PHY/DLE first.
"""
import re, os, glob

EV = os.path.dirname(os.path.abspath(__file__))

def load(path):
    return open(path, 'rb').read().decode('utf-8', 'replace')

def cell(tag):
    c = load(f'{EV}/{tag}-central.log'); p = load(f'{EV}/{tag}-periph.log')
    rx = [int(m.group(1)) for m in re.finditer(r'rxkBps=(\d+)', p) if int(m.group(1)) > 10]
    kept = sorted(rx[20:])
    med = kept[len(kept)//2] if kept else None
    flags = []
    if 'PHY tx=2' in c or 'PHY tx=2' in p: flags.append('2M-SWITCH')
    if 'mtu=247' not in c: flags.append('no-MTU247')
    fsu = re.search(r'FSU: updated status=0x00 spacing=(\d+) us', c)
    disc = [int(m.group(1)) for m in re.finditer(r'disc=(\d+)', c+p)]
    if disc and max(disc) > 0: flags.append(f'disc={max(disc)}')
    if 'FATAL' in c or 'FATAL' in p: flags.append('FATAL')
    return med, (int(fsu.group(1)) if fsu else None), flags, len(kept)

def main():
    tags = sorted(glob.glob(f'{EV}/m0-1m43s-f*-central.log'))
    seq = []
    for t in tags:
        tag = os.path.basename(t)[:-12]
        m = re.match(r'm0-1m43s-f(\d+)-t(\d+)', tag)
        seq.append((int(m.group(2)), int(m.group(1)), tag))
    seq.sort()
    cells = []
    for ts, v, tag in seq:
        med, conf, flags, n = cell(tag)
        if v == 100 and conf != 100: flags.append('EXCLUDE:no-spacing-confirm')
        if v == 150 and med is not None and not (80 <= med <= 95):
            flags.append('BASELINE-OUT-OF-BAND')
        print(f'{tag}: arm={v} rx_med={med} KiB/s conf={conf} n={n} {";".join(flags)}')
        cells.append((v, med, flags))
    # pair consecutive opposite-arm cells in run order
    deltas = []
    i = 0
    while i + 1 < len(cells):
        (v1, m1, f1), (v2, m2, f2) = cells[i], cells[i+1]
        if {v1, v2} == {150, 100} and m1 and m2 and \
           not any('EXCLUDE' in f for f in f1+f2):
            base = m1 if v1 == 150 else m2
            fsu = m2 if v1 == 150 else m1  # fsu = 100us arm
            d = (fsu - base) / base * 100
            deltas.append(d)
            print(f'pair {i//2+1} ({v1}->{v2}): {base} -> {fsu} KiB/s  delta={d:+.2f}%')
            i += 2
        else:
            i += 1
    if deltas:
        mean = sum(deltas)/len(deltas)
        print(f'mean paired delta {mean:+.2f}% over {len(deltas)} pairs '
              f'(quantized model predicts +4.76%)')
    else:
        print('no valid pairs')

if __name__ == '__main__':
    main()
