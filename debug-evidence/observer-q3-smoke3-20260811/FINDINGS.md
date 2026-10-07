# Q3 f100 smoke-3 — DIAGNOSTIC (NON-EVIDENTIARY)

**2026-08-11 · fsu-m0 HEAD fff79e42 · QUARANTINED (`q3-preaccept-smoke`; analyzer
XVAL-DISAGREE)**

Diagnostic re-run with the rev-6 pending-sample latch. Preserved non-evidentiary; **no
promotion.** Manifest hashes all real.

## rev-6 latch — FULLY VALIDATED (the under-sampling is fixed)
`TIFSDIAG prog=808 arm=805 cons_tx=2 cons_done=803 stale_done=3 dup=0 drop=0` — exactly
the preregistered expectation: **hundreds of `isr_done` captures (803)**, ~the two
continued-path `isr_tx` captures (2), **drop=0, dup=0**. Both on-chip bins now populate:
```
TIFSBIN tifs=150 n=371 nv=371 min=120 med=120 max=120 drop=0
TIFSBIN tifs=100 n=434 nv=434 min=120 med=120 max=120 drop=0
```
The pending-sample latch (latch at switch-program, arm on CRC-good RX, consume once at
`isr_tx` OR `isr_done`) works precisely.

## Every other gate PASSES
- peripheral `Q3FSU-DONE role=P … initiator=2 (PEER)` — peer participation PASS.
- whole-cell retention 97.4 % / 98.3 %; phase-retention (F-time) 0.962/0.959/0.98/0.959
  — all ≥ 0.95 PASS.
- plateau shape PRE `{iqr 8, drift 2, conc 1.0}` POST `{iqr 10, drift 2, conc 0.981}` —
  PASS (real-cell-calibrated thresholds).
- **PRIMARY RF step = 800.0 ticks, class=AGREEMENT** (raw first/last-third 802). The
  registered RF gate now runs end-to-end and PASSES.

## The one remaining blocker — CC0 was STALE (READY capture disabled before response TX)
**CORRECTION (per review source audit — the original smoke-3 conclusion below was
WRONG):** `radio_tmr_ready_get()` returned `EVENT_TIMER->CC[TRX]`, but that capture is
armed only at event start (RX READY) and **disabled by `radio_tmr_status_reset()` /
`lll_isr_rx_status_reset()` (radio.c:1211) before the response TX** — nothing re-arms
it for the peripheral's response. So CC0 held the **stale event-start RX READY (~120 µs)
in every event**; the 150-bin's 120 was plausible-but-coincidental and the 100-bin
simply retained the same stale value. `XVAL-DISAGREE` (on-chip delta 0 vs on-air 50) was
caused by the disabled capture channel, **NOT** by READY being invariant to tIFS.

RETRACTED: ~~"CC0 is fixed and cannot track achieved tIFS."~~ This was not established.

FIX (rev 7, narrow): a bench-only HAL helper `bt_ctlr_tifs_rearm_ready_capture()`
re-arms ONLY the RADIO EVENTS_READY → EVENT_TIMER CAPTURE[TRX] PPI (not the recv-timeout
-cancel), called in the peripheral RX ISR after `lll_isr_rx_status_reset()` and before
packet processing, so the response-TX READY is captured. rev-6 latch/labeling/consume
unchanged. Validated at **smoke-4**.

## Valid conclusions RETAINED
- rev-6 pending-sample latch PASSED (TIFSDIAG cons_done=803, drop=0, dup=0; both bins
  populate) — the under-sampling is fixed.
- peer participation PASSED (`initiator=PEER`); whole-cell + phase retention + plateau
  shape PASSED.
- the registered RF PRIMARY reproduced at **exactly 800 ticks** (AGREEMENT).

The on-chip cross-val gate is NOT demoted; smoke-4 tests the re-armed response READY
capture. Only if it stays flat with a correct re-arm do we revisit TX-START / demotion.
