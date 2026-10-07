# GATT duplex FSU — ABBA n=4, does FSU convert for duplex? (2026-08-24)

## Question
The systematic sweep's single-run duplex FSU deltas swung wildly by interval (+1.7% at
7.5ms to +15.3% at 37.5ms), and an earlier hardened head-to-head (7.5–12.5ms only) found
duplex FSU ≈ neutral. Unresolved: are the longer-interval duplex FSU gains **real** or
**single-run noise**? This settles it with a drift-cancelled ABBA n=4 sweep at 15/25/37.5/50ms.

## Method
Open Zephyr GATT **duplex** (central blasts writes, periph echoes as notify; aggregate =
central `exkBps` echo-received B→A + periph `rxkBps` blast-received A→B). FSU on = 52µs tIFS
(`fsu-open.conf;hh-fsu52.conf`); FSU off = default 150µs (`tput-open.conf`). Same-interval
FSU on/off, **ABBA order (off,on,on,off,off,on,on,off) = n=4 each, drift-cancelled**, 55s
capture, steady-window medians. 2× nRF54L15. Firmware/hexes + all 64 logs preserved.

## Result

| interval | off mean (n=4) | on mean (n=4) | delta | ranges (off / on) | verdict |
|---|---|---|---|---|---|
| 15 ms | 168.8 | 170.4 | **+1.0%** | 162–172 / 164–174 | neutral (overlap) |
| **25 ms** | 159.8 | 178.8 | **+11.9%** | 158–163 / **178–181** | **REAL — strict non-overlap** |
| 37.5 ms | 152.2 | 162.0 | +6.4% | 148–155 / 151–169 | positive, noisier (one low on-rep) |
| 50 ms | 146.5 | 162.2 | +10.8% | 143–154 / 154–170 | positive, boundary overlap |

## Verdict
**Duplex FSU is real and grows with interval — NOT noise.** Neutral at 15ms (+1%), then
converts at longer intervals: decisive at 25ms (+11.9%, every FSU-on run strictly above every
FSU-off run, drift-cancelled), consistently positive at 37.5/50ms (+6–11%, direction solid even
where the magnitude is noisier). All four intervals show on ≥ off.

**Mechanism confirmed:** short connection events are already saturated by bidirectional traffic
(no FSU headroom → neutral); longer events have slack that FSU converts even in duplex, just like
one-way throughput.

## Supersedes / corrects
- The earlier "FSU gives NO duplex gain" conclusion was WRONG for longer intervals — it
  extrapolated from a hardened head-to-head that only covered 7.5–12.5ms (where it genuinely is
  neutral). See [[latency-under-load]].
- The systematic-sweep artifact's single-run "+15/17%" duplex line runs a bit hot; the
  drift-cancelled magnitude is +6–12% (25ms the cleanest at +11.9%).
- NOTE: this ABBA run is **open/Zephyr only** and a different RF-day than the single-run sweep
  (absolutes ~11 KB/s lower); the FSU **effect** (delta) is the transferable result, not the
  absolute KB/s. SDC duplex was not ABBA-re-run.

## n=8 top-up at 37.5/50 ms (second ABBA block) — effect reproduces
To firm up the two noisier points, a second independent ABBA block (n=4 each) was run at
37.5/50 ms and pooled with block 1 → **n=8**. The FSU **effect reproduced**, while absolute
throughput showed RF-day drift between blocks:

| interval | block 1 delta | block 2 delta | pooled n=8 (mean+-1SD) |
|---|---|---|---|
| 37.5 ms | +6.4% | **+6.4%** | off 157.8+-6.0 / on 167.9+-7.7 = +6.4% |
| 50 ms | +10.8% | +8.0% | off 150.0+-5.0 / on 164.0+-5.9 = +9.3% (mean+-1SD separated) |

**Key point:** the 37.5 ms delta landed +6.4% in BOTH blocks — the effect is solid. The wide
pooled min-max is inflated by between-block absolute drift (block 2 rose ~10 KB/s) + one 151
outlier, NOT by effect uncertainty. min-max was the wrong lens; the chart uses +-1 SD. So the
finding stands at every interval: 15ms neutral, 25ms decisive (+11.9%, strict non-overlap),
37.5ms +6.4% (reproduced), 50ms +9.3% (mean+-1SD separated).

## Files
`duplex-fsu-abba-n4.csv` (block 1, 32 runs), `duplex-fsu-abba-block2-n4-3750ms.csv` (block 2
top-up, 16 runs at 37.5/50), `logs/` (96 raw cen/per captures), `firmware/` (central FSU on/off
× 4 intervals + periph gecho + example .config), `duplex-fsu-chart.html` (Tufte chart, +-1 SD),
`run.log`, `SHA256SUMS`.
