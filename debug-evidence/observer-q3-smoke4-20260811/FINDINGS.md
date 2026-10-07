# Q3 f100 smoke-4 — DIAGNOSTIC (NON-EVIDENTIARY)

**2026-08-11 · fsu-m0 HEAD bf66b7f4 · QUARANTINED (`q3-preaccept-smoke`; analyzer
INCOMPLETE: whole-cell retention)**

Diagnostic re-run with the rev-7 READY re-arm. Preserved non-evidentiary; **no
promotion.**

## rev-7 READY re-arm — WORKS (the on-chip now STEPS)
The on-chip histogram now reflects the response-TX READY (not the stale event-start RX
READY), and it steps by the FSU amount:
```
TIFSBIN tifs=150 n=388 nv=388 min=140 med=140 max=140 drop=0
TIFSBIN tifs=100 n=205 nv=205 min= 90 med= 90 max= 90 drop=0
TIFSDIAG prog=827 arm=593 cons_tx=2 cons_done=591 stale_done=233 dup=0 rearm=594 cons_norearm=0 drop=0
```
- **on-chip 150→100 delta = 140−90 = 50 µs** (was 120/120 = 0 in smoke-3). The
  smoke-3 "CC0 fixed" reading is fully refuted; CC0 was stale, and re-arming the READY
  capture fixes it. (Absolute values sit ~10 µs below programmed — the proxy stops
  before the on-air bit — but the DELTA is exact, which is what the cross-val uses.)
- **`cons_norearm=0`**: every consumed sample used a freshly re-armed response READY.
- on-air step still reproduced (raw first/last-third **794**).

## Why still quarantined — a TRANSIENT WEAK LINK (not rev-7, not tooling)
`INCOMPLETE: whole-cell retention central 73.9% (300/406)`. This run had a weak
central→peripheral link: the peripheral missed ~25 % of central packets (`cN=406` vs
`pN=305`; TIFSDIAG `arm=593` vs smoke-3's 805), so ~25 % of central TXes got no
peripheral response → unpaired lone-central records → central retention 73.9 %. The
observer itself was healthy (703 records, 0 CRC-bad, 0 ring drops, RSSI −35). The
whole-cell retention gate correctly refused an untrustworthy capture — exactly its job.

## Status
Every controller defect from smokes 1–3 is now fixed and validated:
- responder Host notification (rev-0010) — peer participation ✓
- pending-sample latch (rev-6) — sampling ✓
- READY re-arm (rev-7) — **on-chip step ✓ (delta 50 µs)**

The remaining gate to clear is simply a capture with a stable enough link to pass
≥95 % whole-cell retention (a physical condition), after which the analyzer should
reach METRICS-OK. Re-run: `observer-q3-smoke4b-20260811`.
