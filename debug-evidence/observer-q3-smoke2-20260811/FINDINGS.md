# Q3 f100 smoke-2 — DIAGNOSTIC (NON-EVIDENTIARY)

**2026-08-11 · fsu-m0 HEAD 71db2b03 · QUARANTINED (`q3-onchip-incomplete`)**

Run explicitly as a **diagnostic** to confirm the smoke-1 controller fixes. Preserved
non-evidentiary; **no promotion.** Both worktrees clean; manifest hashes all real
(incl. endpoint ELFs).

## Result 1 — responder Host-notification fix WORKS
The peripheral now fires its callback (was absent in smoke-1):
```
central : Q3FSU-REQ  min=100 max=150 ... rc=0
central : Q3FSU-DONE spacing=100 status=0x00 initiator=0   (local host — central initiated)
periph  : Q3FSU-DONE spacing=100 status=0x00 initiator=2   (PEER)  <-- restored
```
Peer-participation + the new `initiator=PEER(2)` gate both PASS. The cell verdict
advanced `q3-control-plane-incomplete` → `q3-onchip-incomplete`: the entire
control-plane leg is now green.

## Result 2 — on-air step REPRODUCED (robust)
Raw pair `gap_proxy` (first/last-third): **3047 → 2247, step = 800.0 ticks** — exactly
the registered 50 µs target (smoke-1 was 799). The RF observation is reproducible and
unaffected by the controller-reporting fixes.

## Result 3 — TIFSDIAG confirms the on-chip role-path hypothesis (now DATA)
```
TIFSBIN  tifs=150 n=1 nv=1 min=120 med=120 max=120 drop=0
TIFSDIAG calls=2 fresh=2 drop=0 role=1
```
`calls=2` (≈1, not hundreds), `fresh=2`, `drop=0`, `role=1` (peripheral). Per the
preregistered reading this **strongly supports the role-path / hook-placement
hypothesis**: `tifs_on_tx` (in `lll_conn_isr_tx`) is reached only when `is_done==false`
(another exchange continues), so the peripheral's NORMAL one-pair response TX finishes
through `isr_done` and is never sampled — the ~2 calls are the exceptional FSU/control
exchanges. It is no longer a hypothesis; the diagnostic confirms it.

## Still correctly QUARANTINED
`on-chip: on-chip spacings [150] != expected [100, 150]` — the 100 µs on-chip bin is
absent, so the independent on-chip cross-validation leg cannot pass. No promotion or
acceptance decision follows until it does.

## Next (controller, item 4) → smoke-3
Implement a **peripheral-specific TX-completion capture** (read CC0 on the peripheral's
genuine response-TX completion path, role/transition-latched, with correct tIFS
labeling) so the on-chip histogram samples every response — expect hundreds of samples
across both the 150 and 100 bins. Then rebuild/re-export and run **smoke-3, the first
realistic METRICS-OK candidate**. Only a METRICS-OK run (RF step 800±4 AND on-chip
150/100 agreement AND peer participation) may be proposed for the separate f100
acceptance-promotion commit.
