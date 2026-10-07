# Plan: the unmasked FSU throughput measurement (faster pump)

> **Historical planning record.** Kept as written for provenance; its status notes and numbers may
> be superseded or retracted. For current results and their status, see
> [EVIDENCE-INDEX](../EVIDENCE-INDEX.md) and [LESSONS](../LESSONS.md).

*Status: **EXECUTED 2026-08-06 — RESULT PROVISIONAL** (rev 4 relabel per
review: the preregistered validity gate is not fully passed). Phase A
resolved the mask (the receiver's default app-echo doubled the air load —
completion gap 2392 µs ≈ the 2396 µs TX+IFS+echo+IFS airtime model, −4 µs;
sink mode dropped it to 1390 µs ≈ the 1392 µs one-way model, −2 µs —
consistent with air-serialization; the completion callback witnesses buffer
release, not the RF edge, so "air-serialized" awaits the on-air capture).
**U0/U1 measured: FSU throughput gain +15.03%, 95% CI [+12.37%, +17.69%] —
a within-session paired t-CI on one board pair (analysis:
`debug-evidence/sdc-bench-20260806/analyze_u_pairs.py` + `u-pairs.csv`);
U0 146.5–159.8, U1 173.3–180.9 KB/s; airtime model +13.0% inside the CI;
spacing=70 HCI-verified 6/6.** GATE STATUS: continuous-backlog is satisfied
in revised form (formal revision: with blocking-write semantics -ENOMEM is
definitionally impossible; the equivalent evidence is call-block time
tracking the completion gap continuously, which it does); PENDING: the
independent pump-headroom measurement, a formal CPU-idle result, the ±15%
airtime-model sweep, and the qualified on-air spacing capture — plus U2.
The result is a strong provisional controlled measurement until those
close. Plan text below retained as the methodology record (rev 3):* — corrected per external review: the
"scheduler-bound" explanation of the 7.5 ms null is retracted (undetermined,
pump-consistent), the gate now requires positive radio-saturation evidence,
U2 is mandatory, and FSU verification is two-level. Effort: ~half a day plus
instrument qualification.*

## Goal

Measure the Frame Space Update throughput gain in a **demonstrably
air-time-bound** regime (long connection events; every packet-pair carries
two inter-frame gaps: 150 µs FSU-off vs the negotiated 70 µs). Predicted
gain for the GATT instrument ≈ +10–15%; **must be recomputed for the actual
final instrument** (a CoC pump changes the LL frame mix: segmentation,
credit PDUs, different empty-ACK patterns).

## Measured facts, correctly stated (rev-2 correction) — HISTORICAL: superseded by the header above; the bottleneck was subsequently identified as the receiver's echo, and U2's remaining purpose is the short-interval null re-run, not cause discrimination

- T0/T1 at 7.5 ms: FSU on/off = 92.3–94.1 KB/s, ≤1% delta, FSU verified
  active. **Cause of the ceiling: undetermined.** The observed rate is a
  near-constant ~398 writes/s at BOTH 7.5 ms (⇒ 2.99/event) and 50 ms
  (⇒ 19.9/event) — **consistent with a shared instrument-side bottleneck**,
  whose location is undetermined (host TX path, HCI flow control, controller
  buffering/credits, callback path, or CPU scheduling all remain candidates);
  the 50 ms datum *refutes* a 3-pairs-per-event scheduler budget as the
  binding constraint there. Until U2 (below) discriminates, the 7.5 ms result
  is a **controlled but instrument-masked null** — evidence about neither the
  SDC scheduler nor any specific bottleneck.
- Additional datum the diagnosis must explain: pre-DLE (27-byte PDUs) the
  same loop completed ~160 writes/s, not ~400 — the per-write cost is NOT
  constant across PDU configurations, so a naive "2.5 ms/write" pump model
  is also inadequate.
- Reference point: the dedicated open-stack methodology reached ~157 KB/s on
  identical silicon — the pump class exists.

## Approach

**Phase A — timeboxed pump diagnosis (≤2 h).** Instrument the host TX path
(cycle counters around `bt_gatt_write_without_response`, TX-complete
callbacks, Number-of-Completed-Packets cadence) and explain BOTH the ~398/s
post-DLE and ~160/s pre-DLE rates. If a config/one-line fix emerges, take it.

**Phase B — the proven pump: L2CAP CoC credits.** Port the BLE 5 rig's
credit-based CoC blast to the z54 apps under NCS. Recompute the predicted
FSU gain for CoC's actual on-air frame mix before running cells.

## Gate: positive evidence of radio saturation (rev-2 redesign)

A KB/s threshold alone proves nothing — a new ceiling at any level can mask
U1 exactly like the old one. Before the U0/U1 comparison is valid, ALL of:

1. **Radio-capacity response vs a preregistered airtime model:** with the
   pump unchanged, vary a pure radio-capacity control (event length
   10 → 25 → 50 ms, or 2M → 1M PHY) and compare measured throughput against
   the airtime-model *prediction* for each configuration, with a
   preregistered tolerance (±15%). An instrument-bound system is flat; an
   air-bound system tracks the model. This is the primary discriminator.
2. **Continuous backlog:** the sender must show sustained buffer pressure
   (write-attempt rate meaningfully above completion rate / persistent
   -ENOMEM backpressure) throughout the run — the radio never starves.
3. **Headroom margin (independent, rev-3):** pump capacity measured
   SEPARATELY under an overprovisioned radio configuration (radio capacity
   deliberately far above any plausible pump rate), so the pump — not the
   radio — binds in that measurement; it must exceed *predicted U1*
   throughput by ≥20%. Corroborate with in-flight evidence: ACL/CoC credit
   occupancy and Number-of-Completed-Packets cadence showing the controller
   queue non-empty across events. (Persistent -ENOMEM alone proves sender
   pressure, not that the radio never starves.)
4. **CPU non-saturation:** idle-time measurement on the sender during U0.
   If both cells saturate the same CPU/credit/callback bottleneck, the delta
   is masked — NOT valid (rev-2 correction of the earlier caveat, which had
   this backwards).

If the gate cannot be passed, publish the achieved bound and the diagnosis —
not a delta.

## Cells (protocol per `sdc-benchmark-plan.md`: 5 runs, ≥3 min, provenance)

| Cell | Interval / events | FSU | Purpose |
|---|---|---|---|
| U0 | SCI 50 ms, 50 ms events | off | air-bound baseline; gate evidence collected here |
| U1 | same | **on** | **the measurement** |
| U2 (**mandatory**, rev-2) | 7.5 ms | off/on | resolves whether the original 7.5 ms null was instrument-masked or a real scheduler property — the current §15.2b wording depends on this answer |

**Run protocol (rev-4):** U0/U1 executed as **counterbalanced paired runs**
— pair order AB, BA, BA, AB, … (or balanced randomization), an **even**
number of pairs, **≥6** (an odd count leaves an order imbalance) —
so monotonic drift cannot systematically favor one arm (plain ABAB puts
every U0 first and can); warm-up excluded
(the runner's 40 s window, stated in provenance); estimator preregistered as
total received bytes / capture seconds from the receiver's counter deltas;
report the **paired per-pair deltas** with a CI across pairs, not just
per-arm summaries.

## FSU verification — two levels (rev-2)

- Every run: HCI confirmation (`spacing=70`) — proves *negotiation*; a run
  without it is discarded.
- ≥1 representative run per cell: **qualified on-air capture** of the actual
  inter-frame spacing (sniffer qualified per `FSU-proposal.md` phase 4, or
  calibrated RADIO-event instrumentation). Negotiation ≠ physics; the
  headline delta must ride on at least one physically verified run per arm.

## Risks / honest caveats

- CoC under SDC needs its own buffer/credit bring-up; the missing-DLE trap
  (§15.2b) applies to CoC too.
- The gate may reveal an instrument ceiling of ~130–150 KB/s on this path
  whose LOCATION is undetermined (host TX path, HCI flow control,
  controller buffering/credits, callback path, or CPU scheduling — the
  earlier host-pump attribution was retracted); then the deliverable is
  that bound, stated plainly with the location open.
