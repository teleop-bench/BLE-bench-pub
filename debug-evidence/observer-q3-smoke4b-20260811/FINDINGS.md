# Q3 f100 smoke-4b — DIAGNOSTIC (NON-EVIDENTIARY)

**2026-08-11 · fsu-m0 HEAD bf66b7f4 · QUARANTINED (`q3-preaccept-smoke`; INCOMPLETE
whole-cell retention)** — a SECOND rev-7 run, to test whether smoke-4's retention drop
was link luck. It was not.

## rev-7 on-chip step — reproduced (solid)
```
TIFSBIN tifs=150 n=373 min=140 med=140 max=140 drop=0
TIFSBIN tifs=100 n=204 min= 90 med= 90 max= 90 drop=0   -> delta 50 µs
TIFSDIAG prog=811 arm=577 cons_tx=2 cons_done=575 stale_done=232 dup=0 rearm=579 cons_norearm=0 drop=0
```
Same 50 µs on-chip step as smoke-4; `cons_norearm=0`. The rev-7 READY re-arm reliably
fixes the on-chip measurement.

## BUT rev-7 SYSTEMATICALLY perturbs the peripheral RX (2 runs, consistent)
| run | rev | periph CRC-good `arm` | central whole-cell retention |
|---|---|---|---|
| smoke-3 | rev-6 (no re-arm) | **805** | 97.4 % |
| smoke-4 | rev-7 (re-arm) | 593 | 73.9 % |
| smoke-4b | rev-7 (re-arm) | 577 | 69.3 % |

The peripheral's CRC-good reception drops ~28 % whenever the rev-7 READY re-arm is
active, and central whole-cell retention follows (the peripheral misses ~28 % of
central packets → those central TXes get no response → unpaired → retention < 95 %).
The observer is healthy (0 CRC-bad, 0 ring drops, RSSI ~−35); the loss is at the
peripheral's radio, not the observer.

**Conclusion (two levels of confidence):**
- **SUPPORTED:** rev-7 perturbs the peripheral RX (~28 % loss, 2 runs, `arm` 805→~580).
- **HYPOTHESIS (pending smoke-5):** the mechanism is `CC0` (`CC[TRX]`) corruption —
  CC0 is controller-owned as BOTH a capture and a compare register, so re-arming the
  READY capture into it mid-event plausibly corrupts the radio's own RX/switch timing.
  This is NOT yet proven; the dedicated-CC3 run (smoke-5) tests it — if retention
  returns to ≥95 % with CC0 left untouched, the hypothesis holds.

## Record correction: smokes 4/4b did NOT run the gated RF primary
Analysis stopped at the whole-cell retention gate (before the step/cross-val stage), so
the on-air `step` figures quoted for smoke-4/4b are **raw first/last-third REPLAY
diagnostics, NOT the registered gated RF primary**. **Smoke-3 remains the only
end-to-end gated RF pass** (PRIMARY step = 800.0, AGREEMENT). The on-chip 50 µs delta
here is a genuine measurement, but the analyzer verdict was INCOMPLETE (retention).

## The dilemma to resolve (deferred to review — radio-timing call)
- rev-7 makes the on-chip cross-val measurable (delta 50 µs) but costs ~28 % RX.
- Candidate mitigations (need review — my radio-HAL assumptions have been wrong before):
  1. **Disarm immediately after consume** — limit the capture to exactly the
     response-ready window, disabling it again before the next RX.
  2. **Use a SEPARATE EVENT_TIMER CC** (not the shared `TRX` CC) for the bench capture,
     so it never corrupts the radio's own timing.
  3. Accept the on-chip proxy cannot be sampled without perturbation and revisit the
     TX-START / gate-demotion options.

RF result unaffected (on-air step reproduced). No promotion; awaiting the radio-timing
decision before smoke-5.
