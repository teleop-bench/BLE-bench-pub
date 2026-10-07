# open vs SDC GATT duplex @25ms — same-session ABBA n=4 (2026-08-24, rearranged setup)

## Why
The doc's duplex table had open (ABBA, one RF-day) vs SDC (single-run, a different RF-day),
which falsely showed **SDC > open on duplex**. This re-measures BOTH stacks in ONE fresh
session (devices had been rearranged since the earlier runs) so the open-vs-SDC comparison is
apples-to-apples.

## Method
Open Zephyr AND Nordic SDC GATT duplex at 25 ms, aggregate = central exkBps + periph rxkBps,
FSU on/off, ABBA (off,on,on,off,off,on,on,off) = n=4 each, back-to-back in one session.
Open = existing z54-lat hexes; SDC = nRF Connect SDK v3.4.0 (`sdc-sel.conf` etc.). 2x nRF54L15.

## Result — PARITY

| stack | FSU off (n=4) | FSU on (n=4) | FSU gain |
|---|---|---|---|
| open (Zephyr) | 164.2 | 185.8 | +13.1% |
| SDC | 166.0 | 186.0 | +12.0% |

**FSU-on: open 185.8 vs SDC 186.0 — a 0.2 KB/s difference (statistical tie).** Measured fairly,
open and SDC duplex are at **parity**, and **both get ~+12-13% FSU**.

## Corrects
- The doc's old "SDC > open on duplex" (178.8 open vs 181 SDC) was a pure RF-day/mixed-method
  artifact. Same-session → parity.
- The doc's old SDC duplex FSU "+5.2%" was a single-run underestimate; proper ABBA gives +12.0%.
- New arrangement has better RF than the earlier runs (absolutes ~185 vs ~178), so these
  duplex rows sit at a slightly higher RF level than the one-way rows (which are the earlier
  single-run session) — the FSU **effect** is the transferable result, not the absolute KB/s.
- Doc's two GATT-duplex rows updated to these same-session values (open 164.2->185.8 +13.1%,
  SDC 166.0->186.0 +12.0%).

## Files
`open-vs-sdc-duplex-25ms.csv` (16 runs), `logs/` (32 raw captures), `firmware/` (SDC central
FSU on/off + SDC echo periph hexes), `run.log`, `SHA256SUMS`.
