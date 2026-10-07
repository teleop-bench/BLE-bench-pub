# Q3 f100 pre-accept smoke — findings (NON-EVIDENTIARY)

**2026-08-11 · fsu-m0 HEAD f1b343e · QUARANTINED (`q3-control-plane-incomplete`)**

This is the first Q3 mid-step capture on real hardware (2× nRF54L15 + nRF52832
observer). It is **preserved as non-evidentiary** regardless of outcome, per the
acceptance protocol. It does **NOT** meet the promotion bar and **no acceptance
promotion is made**.

## The tooling validated end-to-end
- Fresh f100/fsu builds flashed; **FSU config gate PASSED both endpoints** (tIFS=52,
  1M-only, echo off) BEFORE any flash.
- Connection AA `0x95376875`, map {10,11}, ch10; interval 7500 µs.
- **All Q2 structural + config-binding gates PASS** (814 observer records, zero LOSS,
  oseq contiguous, tx deltas C=418/P=416, distinct DEVICEIDs, observer AA==connection).
- The F-time `M` phase snapshot fired on both roles (`Q3PHASESNAP … tx=415`).
- **Manifest: every hash is a real 64-hex digest** (observer hex/elf/config, both
  endpoint hex/elf/config, all tools + protocols).

## Primary instrument (on-air observer): a STRONG QUARANTINED direct on-air observation
This is a **strong quarantined direct on-air observation** of an ~50 µs tIFS reduction
at the FSU transition — NOT yet a certified physical confirmation (the analyzer did not
return METRICS-OK; peer participation was absent — see below).

Replaying the RF records through the **preregistered timestamp window** (substituting
the central completion time ONLY because the peripheral completion is absent — hence
diagnostic, not registered evidence):

| diagnostic | result |
|---|---|
| pre / post / excluded pairs | 201 / 202 / 4 |
| pre median | 3047 ticks (≈150.5 µs) |
| post median | 2248 ticks (≈100.5 µs) |
| **step** | **799 ticks** (≈ the registered 800 t / 50 µs) |
| pre/post IQR | 6 / 6 ticks (shape gate: PASS) |
| concentration | 1.000 / 0.995 (PASS) |
| central phase retention | 95.3 % / 97.6 % |
| peripheral phase retention | 95.3 % / 98.5 % |
| independent change point | inside the transition window |

So the on-air step survives the real windowed partition + every RF-only gate replayable
from this capture — it is NOT an artifact of the first/last-third split (peer
participation + on-chip cross-val are also automated gates, but need the endpoint logs,
which is why they are listed separately as blockers). The **RF-derived gates pass**; peer
notification and on-chip cross-validation **both** independently remain blockers (a
missing peripheral completion AND a missing 100 µs on-chip bin), not the RF
measurement — both are controller-side, below.

## Why it is (correctly) QUARANTINED — two peer-side gaps
1. **No peripheral `Q3FSU-DONE role=P`** — `frame_space_updated` never fired on the
   peripheral host. The central logged `Q3FSU-DONE role=C spacing=100 status=0x00`,
   but the peer never confirmed adoption. The peer-participation gate refused to
   interpret the capture — exactly the false-positive it exists to prevent.
2. **On-chip histogram degraded** — `TIFSBIN tifs=150 phy=1 n=1` only: a single valid
   sample, and labelled 150 (no tifs=100 bin). Over a 30 s / ~4000-event window this
   should be thousands of samples; n=1 is unreliable and cannot cross-validate.

## Interpretation — both failures are identified controller bugs (not a contradiction)
The observed gap is central-END → peripheral-ADDRESS, so it DIRECTLY measures the
responder's turnaround becoming ~50 µs shorter — the peripheral *did* physically adopt
the reduction. The two "failures" are controller-side reporting/instrument bugs, not
evidence against adoption:

1. **Responder host notification (spec-relevant) — FIXED.**
   `ull_llcp_common.c`: at RX of `LL_FRAME_SPACE_REQ` the responder applied the values
   but DISCARDED `ull_fsu_update_eff()`'s changed flag (`:1083`); at completion the
   second `update_eff` saw no change (masks cleared); and `rp_comm_ntf()` had no
   `PROC_FRAME_SPACE` branch (DLE-only, `:1246`). Fix: preserve the changed flag into
   `ctx->data.fsu.ntf_fsu` at RX + add the `llcp_ntf_encode_fsu_change` branch to
   `rp_comm_ntf`. (Peripheral rebuilds clean; hardware re-run pending.)

2. **On-chip `n=1` — LIKELY a role-path placement issue; DIAGNOSTIC PENDING (not yet
   proven).** `tifs_on_tx()` is called only from `lll_conn_isr_tx` (`lll_conn.c:969`),
   which is installed only when `is_done == false` (another exchange continues); a
   normal one-pair event finishes through `isr_done`, so the lone sample plausibly came
   from the exceptional FSU/control exchange. This is the same role-path distinction as
   the Q2 TX counter, and it is well-supported — but it is a HYPOTHESIS until the
   instrument reports. Rev-5 adds the `TIFSDIAG calls/fresh/drop/role` counters (no
   timing-source/hook-placement change; minor added ISR instrumentation) to decide it
   from DATA: if `calls≈1`, implement a peripheral-specific TX-completion capture with
   correct tIFS labeling; if `calls` is high with low valid, it is a fresh/prime/CC0
   gating issue instead.

## Next sequence
- [x] Fix responder notification (both defects). [ ] Add peer `initiator=PEER` gate
  (done, tooling). [ ] Fix on-chip TX-path placement + counters. Then rebuild, re-export
  the controller patch series, pin new binaries, re-run the quarantined smoke. Promote
  the f100 path ONLY after the UNCHANGED analyzer returns METRICS-OK.
