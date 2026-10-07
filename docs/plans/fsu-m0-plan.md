# Plan M0: bench-grade FSU on the open Zephyr controller (open↔open, benchmarkable)

> **Historical planning record.** Kept as written for provenance; its status notes and numbers may
> be superseded or retracted. For current results and their status, see
> [EVIDENCE-INDEX](../EVIDENCE-INDEX.md) and [LESSONS](../LESSONS.md).

*M0 CLOSURE (2026-08-07): M0 implementation and core bench evaluation
complete; no observed 2M duplex payoff under the registered masked system
test; direct on-air confirmation remains the formal physical gate; the
revised load-ramp campaign is deferred. Phase 4 and the original full
Phase 5 are NOT claimed complete (document propagation, on-air capture,
ramp, and the host-shim patch split remain open).*

*EXECUTED-RESULTS AMENDMENT (2026-08-07): Phases 0-3 built (zephyr branch
fsu-m0, fsu-m0-series.patch); negotiation sweep PASSED (100/70/52 all
SELECTED, fresh connections, zero disconnects). The Phase-4 "throughput-as
-spacing-meter" slope gate below is SUPERSEDED: at 7.5 ms the observed ~5
pairs/event put every arm in the same realized quantization bin (with the
pump ceiling: non-identifying). Replacement executed: 1M / 53.75 ms
quantization discriminator (21-vs-22 pairs), 150-vs-100 us, AB BA BA AB —
PASSED: +5.72% [1.13,10.31] paired throughput + -102/-103 us within-run
completion-gap steps 4/4 (see zephyr-patches/fsu-m0-cheatsheet.md and
debug-evidence/fsu-m0-20260807/PROVENANCE.txt). Indirect evidence only;
on-air capture still pending. The 52 us floor is negotiable via HCI but
1M+tIFS<=60 is a historically failing stock region — the floor soak runs
at 2M/52. Known deviation: no-change requests emit no Complete event. Soak
(2M/52, 780 s + reconnect probe): liveness/stability PASS (2x confirms,
0 disc/timeout/cancel, 10.4 min at floor, default-restoration
witnessed); registered RTT sanity gate FAIL on the soak run
(median-of-per-second-means 12.62 vs ~11.92 ms); Counterbalanced
latency A/B: +28 us [-62,+119] inside the +/-150 us equivalence band,
8/8 valid cells — no practically meaningful reproducible 52-us RTT
effect in fresh runs; a persistent soak-magnitude effect is excluded,
the isolated soak anomaly's cause unidentified (a rare, stateful, or
duration-dependent interaction is not excluded). FSU-off
behavioral regression PASS all bounds. Phase-5: one-way payoff and
duplex D-cell measured (no observed 2M system payoff under masking; see
PROVENANCE); 1M 150->100 mechanism discriminator PASSED. REMAINING:
qualified on-air capture (formal physical gate); the load-ramp arm
(DEFERRED to a separately named follow-up — capture-path amendment
needed); doc-propagation deliverables (exec-summary open-FSU row, charts);
host-shim patch split. Ramp is hypothesis-generating and not required for
the established findings.*

*Status: plan rev 2 (2026-08-06) — protocol model corrected per external review (no shared instant; responder-adopts-then-responds; LL_REJECT_EXT_IND; feature-bit-65 event gating; corrected gate arithmetic with slope/residual test; 2M-first). Realizes a deliberately de-scoped slice of
`FSU-proposal.md` phases 0–3: enough Frame Space Update in the open
`ll_sw_split` controller to run tonight's benchmark cells against it —
explicitly NOT upstream-mergeable (that is M2). Effort estimate: 2–4 focused
days. Everything downstream of the controller already exists: the host API
is in-tree, our central app already calls it, and the U/duplex/ramp harnesses
plus SDC reference numbers (70 µs floor, +15% provisional throughput gain,
the hypothesized under-load effect) are committed.*

## Scope (and the carrot)

- **In scope:** ACL connections only; **spacing reductions only** (150 µs
  down to a floor we choose); one spacing value, both ACL directions,
  **2M PHY only** for the payoff/duplex/soak cells (rev-2); EXCEPTION:
  the completed 1M 150->100 us mechanism discriminator ran at 1M by
  design (the demonstrated spacing-responsive geometry) — never 1M/52,
  which was not sustained on this rig in this campaign. Central-initiated only;
  open↔open.
- **The carrot:** the SDC's floor is 70 µs; our silicon ran **52 µs**
  sustained in earlier latency work (stock low-latency mode — PHY of that
  stable point contested, see Risks). M0 targets a 52 µs floor —
  the open stack potentially out-FSU-ing the proprietary one on every chart.
- **Non-goals (deferred):** SDC interop and the real Extended Feature Set
  page exchange (M1); upstream quality, llcp test battery, collision
  handling beyond reject (M2); spacing increases; CIS/MCES spacing types;
  per-PHY distinct values.

## Phase 0 — spec extraction (½ day, blocks everything)

From Bluetooth Core 6.0, Vol 6 Part B (public download):
1. Exact **LL_FRAME_SPACE_REQ / LL_FRAME_SPACE_RSP** PDU formats: opcodes,
   field layout (spacing value(s), types mask, PHY mask), units.
2. The **adoption timing** rules (rev-2 correction: there is NO shared
   "activation instant" — the external review, citing Core 6.2 §5.1.30,
   describes: the RESPONDER adopts the new spacing before sending
   LL_FRAME_SPACE_RSP; the INITIATOR switches no later than the sixth anchor
   after receiving it; transitional receive windows bridge the asymmetric
   interval. Phase 0 verifies each element verbatim against the spec text —
   the earlier "common instant" placeholder would have built exactly the
   timing disagreement it feared).
2b. **Selection semantics:** the responder SELECTS a value within the
   requested range; the HCI Complete event reports the SELECTED value, not
   the requested one. Rejection is via **LL_REJECT_EXT_IND** (there is no
   "RSP with error"), and an FSU-capable controller must support Extended
   Reject Indication — in scope.
3. Valid ranges and mandatory rejection behavior (what a compliant peer must
   answer when unsupported — matters for M1, recorded now).
Deliverable: a half-page PDU/adoption-timing cheat-sheet checked into
`zephyr-patches/` alongside the eventual patch.

## Phase 1 — plumbing (½–1 day)

- `pdu.h`: PDU structs + opcodes per Phase 0.
- `Kconfig.ll_sw_split`: declare `BT_CTLR_FRAME_SPACE_UPDATE_SUPPORT`.
- **Host-gating shim (documented, non-mergeable; rev-2 rescope):** relaxing
  the Kconfig dependency is NOT enough — `hci_core.c` only unmasks the FSU
  Complete event when **feature bit 65** appears in the local feature set
  (`BT_FEAT_LE_FRAME_SPACE_UPDATE_SET`, hci_core.c:3638). The M0 shim must
  therefore either (a) implement minimal local-feature reporting including
  bit 65 + the supported-commands bits, or (b) explicitly force the feature
  state and event mask in a clearly-isolated bench patch. Either way it is
  the #1 reason M0 is not upstreamable — stated here so nobody forgets.

## Phase 2 — the procedure (1–1½ days)

- New local + remote procedure in `ull_llcp`, cloned from an existing
  two-PDU procedure's skeleton (CTE request or min-used-channels are the
  closest patterns in-tree).
- Happy path: central initiates with a range spanning the current value;
  peripheral SELECTS and adopts before its RSP; central switches within six
  anchors with the transitional receive windows per spec. Reject path:
  **LL_REJECT_EXT_IND** (Extended Reject Indication in scope). Collision
  policy for M0: **single-initiator convention** (only the central ever
  initiates; a remote-initiated request is rejected). Documented as a bench
  simplification. Sweep protocol (rev-2): since M0 refuses increases, the
  150→100→70→52 sweep uses a **fresh connection per step**, each request a
  range containing the current value, verifying the SELECTED value each
  time.
- Commit path: write the agreed value into `lll->tifs_tx_us / tifs_rx_us /
  tifs_hcto_us` (the fields the stock low-latency mode already proves out)
  per the asymmetric adoption timing above (responder pre-RSP; initiator
  within six anchors, with transitional RX windows). Reductions-only means existing
  scheduler slot reservations stay conservative — no `ticks_slot` rework.

## Phase 3 — HCI bridge (½ day)

Wire the controller side of the already-merged host surface: handle the LE
Frame Space Update command (types already in `hci_types.h` in our tree),
initiate the llcp procedure, and emit the LE Frame Space Update Complete
event on procedure end — which lights up the existing
`frame_space_updated` callback in our unmodified central app.

## Phase 4 — bench validation (½–1 day, gates the benchmarking)

Pre-registered acceptance before any cell results are quoted:
1. **Negotiation evidence:** HCI complete event reports the SELECTED value
   at each step of the fresh-connection sweep 150 → 100 → 70 → 52 µs, both
   boards' logs consistent.
2. **Physics evidence via the throughput-as-spacing-meter (rev-2
   corrected):** completion-floor model = 1048 + 44 + 2×spacing at 2M/244 B:
   **1392 @150, 1292 @100, 1232 @70, 1196 @52 µs**. A ±5% band cannot
   resolve 70-vs-52 (a 2.9% difference), so the gate is a **preregistered
   regression across the four-point sweep**: fitted slope within
   2.0 ± 0.3 µs per µs-of-spacing AND per-point residuals < 15 µs. The
   completion floor is corroboration, not physical proof — the qualified
   on-air capture (or calibrated GPIO timing) remains the physical gate,
   inherited from the FSU plan's caveats.
3. **Stability:** ≥10 min at the floor with zero timeouts/disconnects.
4. **Regression (rev-2 split):** if FSU compiles out completely, compare
   build hashes (identity check); otherwise preregistered behavioral bounds:
   FSU-off RTT mean within ±1% of today's open baseline, zero new
   disconnects/timeouts over a 10-minute soak, controller diagnostics
   (trx-busy counter) unchanged.

## Phase 5 — the payoff cells (REWRITTEN post-Phase-4 per review; the
## original >=16%-at-2M/52 prediction and 150/100/70/52 sweep are
## SUPERSEDED by the demonstrated pump/quantization masking and the
## unsupported 1M/52 region)

- **2M system-payoff A/B:** same FSU-enabled build family, blast+sink
  rig; A = request [150..150] (control), B = [52..150] (negotiates 52);
  counterbalanced AB BA BA AB. A null is interpreted ONLY as "observed
  system payoff nil under pump/quantization masking" — NOT absence of a
  radio-level benefit.
- **Duplex D-cell:** echo configuration, aggregate AND per-direction
  endpoints, spacing confirmation + symmetry checks per the duplex
  contract conventions, matched A/B controls (same request-pair design).
- **Ramp arm:** paired/interleaved A/B arms, deterministic per-stage
  segmentation (loadramp rev-2 analyzer conventions).
- **Mechanism result:** the TWO-POINT 1M 150-vs-100 us comparison —
  reuse the completed series or independently replicate the demonstrated
  53.75 ms geometry; never presented as a four-point sweep and never
  implying 1M/52 was measured (it wasn't; that region kills the link).
- All validity gates, estimators, exclusion rules and CI rules fixed in
  committed analyzers BEFORE any cell log is read.

## Deliverables & hygiene

- Patch series exported to `zephyr-patches/` (`fsu-m0-*.patch`) with base
  commit pinned (`1f6485ec`) and the README updated — same provenance
  convention as the existing patches; the host shim as its own
  clearly-labeled patch.
- Results into §15 + charts with an **"open-FSU (M0 bench implementation)"**
  label — never conflated with a spec-qualified implementation.
- `FSU-proposal.md` updated: M0 executed, M1/M2 deltas restated.

## Risks (pre-registered)

- **Adoption timing** misread → the two ends briefly disagree on spacing
  → the Phase-4 physics check would surface it as a floor mismatch; treat
  any such mismatch as a stop-and-fix, not noise.
- llcp framework subtleties (procedure contexts, memory pools) — mitigated
  by cloning an in-tree procedure wholesale.
- **PHY of the historical 52 µs stable point: confirmed 2M** (a stale main.c
  "1M" comment was corrected in the record: "all prior 1M numbers were
  actually 2M", proven by forcing 1M). M0 stays
  **2M-only, both ACL directions**, includes reconnect/default-restoration
  testing, and Phase 4 re-tests the 52 µs point as a REPRODUCIBILITY check
  (1 sustained run out of ~6 historically) — not as an open historical
  question. 1M is a separate floor campaign, never auto-assigned 52 µs. If
  52 µs is unstable at 2M+DLE under load, fall back to 70 µs and record the
  floor as open.
- The host shim can drift from real host behavior — M1 replaces it before
  any interop claims.

## Phase 6 — roadmap to a physically-qualified, unmasked benchmark suite

*Roadmap recorded 2026-08-07; each campaign requires a frozen
preregistration before data collection (this section is a durable
STRATEGY record, not an executable protocol). Principle: design each test
so that a gain, a HARM, or practical-equivalence/null is each identifiable
— never design for a positive result. Ordered by shortest-path-to-defensible-claim.*

**Standing constraint (applies to every cell below, promoted from
"last"):** any open-Zephyr-vs-SDC comparison uses matched application
transport, PHY, payload, event length, buffers, duration, estimator, AND
spacing reduction — both controllers must first run the SAME reduction
(150→70 µs, the SDC's floor). Open Zephyr's 150→52 µs is an ADDITIONAL
open-only cell; a different spacing reduction on each side cannot
establish controller superiority. Report absolute capacity AND
within-controller % improvement separately. NEVER compare the Zephyr 1M
mechanism cell against the SDC 2M/GATT +15% result (apples-to-oranges).

### 6.1 On-air capture — close the physical gate (shortest path to correctness)
Capture the inter-frame spacing on a representative run per arm, in BOTH
geometries: 1M 150→100 µs (behind the measured +5.72%) and 2M 150→52 µs
(the flagship point). Instrument distinction: a QUALIFIED SNIFFER measures
on-air spacing directly; calibrated RADIO-event GPIO is a physical-timing
PROXY, not literally on-air. Either demonstrates STEADY-STATE spacing
application — NOT full FSU procedure correctness and NOT spec conformance.
Supports only: "the M0 controller applies the selected steady-state frame
spacing." Capture qualifies the MECHANISM; it does not make any throughput
NUMBER more precise. Its own frozen preregistration must fix, before
recording: capture count per arm, the post-transition settle window,
measurement uncertainty, which directions are captured, and the
accept/reject rules.

### 6.2 Publish the scoped 1M result (shortest path to a defensible win)
After 6.1, the existing result upgrades to a physically-qualified
controlled win — but NOT published bare. Required scope in the headline:
- "In a radio-bound 1M geometry (150→100 µs, open↔open, bench-grade M0),
  open Zephyr FSU improved throughput by +5.72%, 95% CI [+1.13, +10.31]."
- Caveats that travel WITH the number: measured under sustained trx-busy
  cancellation (~17/s, asserts-off -ECANCELED fallthrough, arm-matched);
  n=4 pairs; CI lower bound +1.13% barely excludes zero. The registered
  PRIMARY endpoint was throughput; the −102/−103 µs within-run
  completion-gap step (4/4 cells, model −100) was the registered SECONDARY
  endpoint and is the STRONGER MECHANISTIC CORROBORATION — the original
  hierarchy is preserved (no retroactive promotion).

### 6.3 The identifiable 2M throughput flagship (long path; may be blocked)
Build an unmasked, radio-bound 2M instrument:
- 50 ms (or model-selected long event) chosen so 150 and 52 µs give
  DIFFERENT packets-per-event. Illustratively, the existing GATT model
  gives 35 vs 41 pairs; final geometry and headroom are TBD from the CoC
  frame model. A faster pump at 7.5 ms does NOT help (both arms share the
  same ~5-pairs/event bin).
- Replace the limiting sender with the previously demonstrated L2CAP CoC
  credit pump design (or an equivalently fast nonblocking pump) — NOT yet
  established for the new M0 CoC instrument; sink peripheral, NO mirror
  echo; deep buffers; controller queue continuously occupied.
- **Pump-headroom gate as a STOP condition:** independently demonstrate
  pump capacity ≥20% above predicted FSU-on throughput; verify CPU
  headroom and NoCP/credit occupancy. **HONEST RISK:** the 52 µs geometry
  at 50 ms is air-bound only above ~195 KB/s FSU-on (ILLUSTRATIVE — the
  35-vs-41-pairs figure uses the 244-byte GATT pair model; the final CoC
  instrument changes framing + credit traffic and its model MUST be
  recomputed from the CoC frame mix before use). Under that illustrative
  model the ≥20% headroom gate is ~234 KB/s, not merely ">190". It is
  UNKNOWN whether the chosen open CoC pump clears the previously observed
  GATT-path ~157 KB/s ceiling (location undetermined; not yet established
  for the M0 CoC path; SDC reached ~178 at 50 ms, open stack never shown
  >~158). If the gate cannot be passed, the deliverable is that
  BOUND, stated plainly — not a delta, and not a failure.
- Cell sequence (matched-comparison constraint made explicit to prevent
  omission): FIRST the matched 150→70 µs pair on BOTH controllers
  (Zephyr [150..150] vs [70..150]; SDC 150 vs 70), same-build A/B,
  counterbalanced ≥6 pairs each; THEN the ADDITIONAL open-only Zephyr
  150→52 µs cell ([150..150] vs [52..150]).

### 6.4 Under-load latency via the repaired ramp (NEW instrument, not just a fix)
Idle serialized RTT is anchor-locked and already equivalent — it may never
yield an FSU win; do not chase one there. The useful experiment:
- Repair CDC interference by aggregating statistics in RAM (one compact
  per-stage/end summary) — recorded as a preregistration amendment, with a
  full-ramp smoke before restarting cells.
- ADDITIONALLY instrument ONE preregistered latency estimand — either
  enqueue→controller-completion OR enqueue→application-acknowledgement
  (NOT interchangeable; pick and freeze one). The current rig only times
  serialized ping-pong RTT, so this is a NEW measurement.
- Offered load must be OPEN-LOOP and identical between arms; completion-
  driven (closed-loop) sending hides queueing by throttling to capacity.
- Sweep load through the FSU-off knee; report median, p95/p99, backlog,
  loss, and the highest load meeting a preregistered latency bound.
  SYMMETRIC OUTCOMES (not a "likely claim"): FSU either (a) moves the
  saturation knee / preserves bounded latency at higher offered load,
  (b) shows no identifiable knee shift within the swept range, or
  (c) WORSENS the knee or latency — all three are reportable results;
  "idle RTT fell" is NOT among the questions.

### 6.5 Duplex where the radio binds (NEW firmware: independent sources)
Reuse the demonstrated 1M/53.75 ms radio-bound geometry:
- INDEPENDENT sustained streams both directions — the D-cell's coupled 1:1
  echo config is insufficient (duplex-plan.md flagged independent sources
  as future work); this is a firmware build, not a config change.
- Deep buffers, continuous backlog both endpoints; 150 vs 100 µs, matched,
  counterbalanced; aggregate + per-direction. The 5% symmetry threshold is
  a REPORTING FLAG, not an exclusion (duplex-contract convention).
- Predicted capacity change must be RECOMPUTED from the independent-stream
  frame mix — the one-way ~4.8% figure CANNOT be inherited (independent
  bidirectional traffic has a different pair/ACK structure). Progression
  is VALIDITY-based, not positive-result-conditioned: if the instrument
  passes its preregistered identifiability gates, report the result
  (gain, harm, or equivalence); diagnose any material model disagreement
  BEFORE deciding whether to port it to 2M/52 with the 6.3 pump +
  long-event geometry.

### Shortest-path summary
1. Physical-timing qualification (6.1) — closes the STEADY-STATE
   PHYSICAL-SPACING gate in both geometries (a sniffer qualifies the
   on-air claim; GPIO is a timing proxy).
2. Publish the scoped, caveated +5.72% 1M throughput result (6.2).
3. Build the long-event CoC instrument for the 2M flagship (6.3) —
   may terminate in "mask not removed by this instrument/configuration"
   (exhausting one CoC implementation cannot establish a SILICON limit).
4. Repaired + extended ramp for under-load latency (6.4).
5. Reuse the radio-bound geometry for duplex (6.5).
Capture closes the steady-state physical-spacing gate; the radio-bound
pump + geometry are what create an identifiable system-payoff benchmark.
Physical-timing qualification is the right next move regardless — it is
the shortest path to closing that gate AND (via 6.2) to the first
defensible published win; it is NOT by itself the path to the 2M
throughput/latency/duplex flagship.
