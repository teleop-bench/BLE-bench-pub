# Plan: degraded-RF false-trip campaign (§14.9 watchdog under bad radio)

> **Historical planning record.** Kept as written for provenance; its status notes and numbers may
> be superseded or retracted. For current results and their status, see
> [EVIDENCE-INDEX](../EVIDENCE-INDEX.md) and [LESSONS](../LESSONS.md).

*Status: plan rev 3 (2026-08-06) — corrected per external review: split into
SURVEY and TRIP-VALIDATION modes (the resetting watchdog right-censors gap
data), retrospective trip classification, armed-hour reporting, specified
persistence and statistics, foil safety. No new parts required.*

## Goal

Under progressively worse RF, produce the **deadline-selection curve**: for
any candidate deadline X in the observation range, how many times per armed
hour would it have fired on a link that subsequently recovered ("false"
trips) vs on a link that was genuinely dying. Plus: HB gap distribution
tails, disconnect/reconnect behavior, and validation that the real 200 ms
watchdog behaves correctly when it does fire.

Stack: **open Zephyr** (the safety rig's stack; §15.3 showed the chain is
controller-agnostic at this scale).

## Two modes (rev-2 core redesign)

**Mode 1 — SURVEY (primary; most of the campaign hours).**
The observer must not reset itself mid-observation: the production 200 ms
hardware watchdog *destroys the evidence* (a gap is right-censored at the
reset, its eventual length unknowable, and post-loss behavior unobservable).
Survey firmware (Kconfig `CONFIG_APP_WDT_SURVEY`, clearly BENCH-ONLY):
- **Reset DISABLED** (rev-3): the hardware watchdog is either not started
  or fed by a local timer independent of link state. A merely-lengthened
  deadline (≥5 s) is NOT acceptable — measured reconnects run 3–9 s, so a
  5 s deadline could reset mid-recovery and recreate the censoring this
  mode exists to remove.
- **Complete HB gap recording**: every inter-heartbeat gap >50 ms logged
  with its actual duration (timestamped serial line) + fine local bins
  (50–500 ms in 25 ms steps, then 0.5–5 s log-spaced, + overflow/censored
  count). Scope note (rev-3): with a ~20 ms nominal heartbeat period, gaps
  ≤50 ms are deliberately unrecorded — the promised statistics are therefore
  **exceedance rates and gap-tail distributions conditional on >50 ms**, not
  unconditional p95/p99 of all inter-heartbeat times.
- **Virtual thresholds**: counters for "a deadline of {100, 150, 200, 250,
  300, 500} ms would have fired here" — evaluated on complete gaps.
- **Retrospective classification** (rev-2 correction): at expiry-time the
  link state is uninformative — after RF death the connection still reads
  "up" until the ~4 s supervision timeout, so a real loss masquerades as a
  gap-trip. Classify each virtual trip AFTER the fact: **gap-trip** = the
  same connection resumed valid heartbeats with no disconnect **within a
  preregistered 30 s classification horizon**; **loss-trip** = a disconnect
  occurs, or the horizon expires without resumption. Gaps still open at run
  end are **censored** and reported as such, never classified. Each complete
  gap contributes **exactly one event per virtual threshold it exceeds**.
  Log enough context (gap start time, disconnect events) to classify
  offline from the serial record.
- Survey mode does not reset ⇒ counters live in RAM; persistence is not
  load-bearing here.

**Mode 2 — TRIP-VALIDATION (short, separate runs).**
The real 200 ms hardware watchdog, at 1–2 degraded conditions. **Claim scope
(rev-4):** counters and serial evidence validate **trip / reset-to-safe /
auto re-arm behavior only** — they cannot witness the physical pin. The
STOP-pin edge under degraded RF is either (a) verified by attaching the
analyzer for ≥1 representative trip per condition, or (b) explicitly
inherited from the clean-RF §14.10 pin-level evidence, stated as such in the
writeup. Because this mode resets:
- **Reset-surviving counters** via `.noinit` RAM, **dual-slot**: two
  versioned records with magic + sequence + CRC, written alternately. Note
  (rev-5): dual-slotting preserves *historical* state — if the reset tears
  the slot being written, the older slot restores the previous count and the
  just-occurring trip would be lost. Therefore **boot reconciliation**: when
  the newest slot is invalid AND the hardware reset-cause register says
  watchdog, increment the older record by the implied trip before use. The
  pre-campaign **forced-reset test injects resets at each write phase**
  (before, mid-write, after) and must show no lost or double-counted trips.
  Counters: trips, boots, last-gap-before-trip, cumulative armed time.
- Auto re-arm (`CONFIG_APP_WDT_CAMPAIGN`) — production stays latched; this
  is bench-only and labeled.

## Reporting semantics (rev-2 correction: auto re-arm DOES affect rates)

Reset, reconnect, re-arming dwell and STOPPED time all subtract exposure.
Report per condition:
- virtual/actual trips **per armed-RUNNING hour** (exposure-normalized);
- **wall-clock availability** separately (fraction of time armed+running);
- **time to first trip** — the statistic closest to latched production
  semantics.

## Acceptance criteria and statistics (rev-3: pre-registered)

**Acceptance bound (planning assumption — the real bound must come from the
safety owner, like the stop-time budget):** λ_max = **0.1 false (gap-)trips
per armed hour**, at 95% confidence. **The PASS rule is evaluated per
candidate deadline** (rev-4): the primary candidate is **200 ms** (the
production value); the virtual-threshold curve reports the others. A
candidate deadline X **passes** only if its gap-trip upper bound is below
λ_max at **every** C0–C2 condition.
- **PASS (zero-event form, Poisson-model-qualified):** zero gap-trips at
  threshold X over T ≥ −ln(0.05)/λ_max ≈ **30 armed hours, PER CONDITION**
  (rev-5: since X must pass independently at C0, C1 and C2, a zero-event
  PASS costs ≈30 armed hours at *each* condition — ≈90 total — not 30
  pooled). Explicitly a Poisson-model result (a block bootstrap over
  all-zero blocks is degenerate and cannot certify confidence under
  clustering). The **≥3 independent sessions on ≥2 different days — per
  condition** requirement is a **sampling safeguard** against
  one-continuous-run artifacts, not a statistical guarantee of robustness to
  clustered arrivals (rev-5 relabel); a session-level model would need
  substantially more independent sessions and is out of scope.
- **PASS (events-observed form):** 95% upper bound < λ_max using the
  block-bootstrap interval (primary).
- **FAIL**: 95% lower bound (block bootstrap) > λ_max.
- **INCONCLUSIVE**: otherwise — reported as such, with the exposure needed
  to resolve.
C3/C4 are characterization conditions (no pass/fail; the deliverable is the
measured curve).

- Zero events in 1 h only bounds the rate at ≲3/h (95%, rule of three) —
  the exposure formula above, not intuition, drives dwell. Minimum dwell
  **2 h per condition** for the curve; PASS claims require the full exposure.
  Report 95% CIs with every rate.
- **Dependence caveat (rev-3):** trips cluster (interference/fading), so
  independence-based intervals can be falsely narrow. Primary intervals via
  **block bootstrap over 10-minute blocks**; exact Poisson alongside as a
  labeled reference model. Replicate time blocks across days where possible.
- **Interleaved controls**: re-run C0 (baseline) between degraded conditions
  to detect time-varying ambient interference.
- Stop rule per condition: stop early only when ≥10 events collected
  (rate well-determined) or the pre-registered dwell is reached.

## Physical setup

- Central: Mac mini, serial logged live.
- Peripheral: remote, USB power (charger/power bank); **preferred: the Linux
  box** (power + serial via a provided one-line command, user-driven per
  standing practice).
- **Foil safety (rev-2):** never wrap an energized DK directly in foil —
  insulate first (plastic bag/box), then foil, with clearance around the
  board. Record per condition: geometry photo/note, board orientation, and
  RSSI / link-quality proxies where available from the central.
- Logic analyzer not required; nRF52 external watchdog optional (only
  meaningful in trip-validation mode).

## Conditions matrix (survey mode; dwell per statistics section)

| # | Condition |
|---|---|
| C0 | same room ~1 m — control, interleaved between all others |
| C1 | 1 wall / adjacent room |
| C2 | 2+ walls / far end of dwelling |
| C3 | C1/C2 + insulated-foil wrap |
| C4 | worst condition that still connects intermittently |

Then trip-validation runs (Mode 2) at C0 and one interesting degraded
condition.

## Deliverables

- A new write-up section: the deadline-selection curve
  (virtual-threshold trips per armed hour vs deadline, per condition, with
  CIs), gap distributions with tails, gap-vs-loss breakdown, reconnect
  times, and the Mode-2 validation results.
- Executive-summary update: degraded-RF gate → measured (whatever the
  numbers are).
- Evidence bundle under `debug-evidence/` (PROVENANCE/sha256), including the
  raw timestamped gap logs.

## Honest caveats (pre-registered)

- Foil+walls is uncalibrated attenuation: conditions are reproducible
  descriptions, not dB figures (calibrated attenuators remain future work).
- Survey mode's non-resetting observer deviates from production semantics by
  design; Mode 2 exists precisely to validate the production behavior.
- One dwelling's RF ≠ the world: this produces the method and the curve
  shape; site-specific qualification remains the deployer's job.

## Added objective (2026-08-24): GATT vs CoC throughput robustness under degraded RF

**Question:** does L2CAP CoC throughput degrade faster than GATT as RF worsens?
**Motivation (incidental, n=1):** during the full-matrix re-run, moving the two
nRF54 boards from adjacent → separated dropped **open CoC ~170→~128 KB/s while
GATT held ~170**; adjacent recovered CoC to ~164. So CoC sat ~30% below GATT at
worse placement vs ~14% adjacent — CoC recovered far more when RF improved. If
real, GATT is the RF-robust choice for a congested/steel-hull shipyard (a FOURTH
reason to lead with GATT for teleop — see the `gatt-vs-coc-teleop` note). But it's
one uncontrolled arrangement change; this campaign should make it a claim, not a
hypothesis.

**Method (fold into the SURVEY conditions matrix):** at each RF condition (the
same stepped foil/wall/distance ladder as the watchdog survey), measure
**one-way saturated throughput for GATT and CoC, open and SDC**, plus loss /
retransmit / disconnect. Reuse the recovered benchmark apps (`z54-lat-central/periph`,
`coc-central/coc-sink`) and the matrix sweep harness. Report **throughput vs RF
condition per transport** and the **CoC-minus-GATT gap vs RF** (the key curve).

**Mechanism to test:** CoC's segmentation + credit-flow stalls under retransmits
(credit-return PDUs round-trip slower, window stalls) so it collapses faster than
raw loss rate; GATT notify is fire-and-forget and degrades gracefully. Prediction:
the CoC-minus-GATT gap **widens monotonically** as RF worsens.

**Scope note:** this is the BULK-telemetry path. The tiny heartbeat/stop-signal has
no bulk credit flow, so its RF behavior is a separate question (covered by the
survey's HB-gap tails above) — don't conflate the two.
