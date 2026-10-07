# Technical statement for external review — Is the CoC one-way "FSU throughput gain" real?

> ## ⛔ RETRACTED / SUPERSEDED (2026-09-08) — the conclusion below is WRONG
> This document concluded "CoC one-way FSU ≈ 0%, transport-dependent, root cause = below-ceiling
> RF/session." **That is incorrect.** A subsequent review found the measurements were invalid: the
> responder's ~5 s GAP auto-param-update **silently reverted tIFS to 150 µs mid-run**, and the harness
> verified FSU only via a boot-time token that is **latched/stale** (keeps printing `fsu=52` after the
> revert). Re-measured with **auto-update-OFF sinks and held-verified end-to-end**, the corrected result is:
> **CoC one-way FSU ≈ +20% (held), equal to GATT — NOT 0%, NOT transport-dependent.** The "non-reproduction
> of the August +14.4%" was the same artifact (my replay sinks reverted). Corrected evidence:
> `debug-evidence/{coc-fsu-corrected,sdc-fsu-corrected}-20260908/`. Held-verified 2×2 (both stacks):
>
> | | open (Zephyr 4.4.2) | SDC (NCS 3.4.0) |
> |---|---|---|
> | CoC (15 ms) | **+20.4% ± 1.0%** | +11.6% ± 0.8% |
> | GATT (7.5 ms) | **+19.7% ± 0.5%** | +25.1% ± 0.5% |
>
> This file is retained for the *method* (hypothesis elimination) and as a case study in the failure it
> documents. **Do not cite its conclusions — the authoritative results are in
> [`fsu-benchmark-technical-report.md`](./fsu-benchmark-technical-report.md).** The durable lesson is
> `docs/LESSONS.md` #1 (verify the treatment is held THROUGHOUT; a token ≠ live state), now enforced by
> `tools/verify_run.py`.



**Status:** resolved on hardware, 2026-09-08. **Scope:** L2CAP CoC (Connection-Oriented Channel),
one-way, open-source Zephyr `ll_sw_split` controller + our `fsu-m0` Frame-Space-Update patches, on
2× nRF54L15-DK, 2M PHY + DLE-251. **Raw data & firmware:** `debug-evidence/{coc-fsu-rootcause-20260908,
coc-fsu-interval-20260908,fsu-campaign-20260907,coc-open-fsu-20260813}/`. This document is written to be
falsified: the falsifiable claim, the tests that would break it, and the raw artifacts are all stated.

## Summary
An earlier measurement reported a **+14.4% CoC one-way throughput gain from Bluetooth-6 Frame Space
Update** (FSU; inter-frame spacing tIFS 150→52 µs at 2M). We were unable to reproduce it and
investigated to root cause. **Finding: the +14.4% — and the broader +9–24% CoC one-way FSU envelope —
were artifacts of a measurement session in which the link happened to run *below* its airtime ceiling.
They are not a reproducible property of CoC, the configuration, the controller version, or any
diagnostic build option.** The identical original firmware binaries, and a bit-faithful rebuild of the
full original software stack, reproduce **≈0%** today. The general result: **CoC one-way FSU converts to
goodput only when the connection event is limited by the inter-frame gap with data queued to fill the
freed time; at the clean-bench saturation ceiling it is a no-op.** GATT one-way FSU (**+20%**), by
contrast, reproduces robustly at the ceiling.

## Falsifiable claim
> On an at-ceiling one-way CoC link (open controller, 2M/DLE-251, clean bench), enabling FSU
> (verified `spacing=52` on both ends) produces **0% ± ~2%** throughput change. A positive CoC one-way
> FSU delta requires a *below-ceiling* baseline (event occupancy below the airtime maximum).

This is refuted by any at-ceiling CoC one-way run (off-baseline within ~3% of the ~160 KB/s ceiling)
that shows a repeatable FSU gain > ~2% with `spacing=52` verified from boot.

## Method
1. **Verify engagement, from boot.** The FSU-negotiated-`spacing=52` line prints once at connection
   setup; a mid-stream capture misses it. All measurements open serial *before* reset and confirm
   `spacing=52` (FSU arm) / absence (control arm) per run.
2. **Counterbalanced repetition.** Arms alternated per round so slow drift cancels from the paired
   per-round delta; 95% CIs reported (within-session precision).
3. **Strongest-possible reproduction.** To separate firmware/config from environment we (a) reflashed
   the *exact archived original binaries*, and (b) rebuilt the *complete original stack on the original
   controller version* (v4.4.1), and measured both today.
4. **Occupancy instrumentation.** `RPT occ: mean_pkts/ev` (per-event transmit count) read directly, to
   see whether FSU adds packets to the event or not.

## Results (2M, 244 B unless noted; KB/s = /1024; mean ± 95% CI)

**(R1) At-ceiling counterbalanced campaign, n=60 rounds, 15 ms/480 B** (`fsu-campaign-20260907`):

| pair | off | on | FSU delta |
|---|---|---|---|
| GATT / open | 157.7 | 189.6 | **+20.2% ± 0.1%** (100% engaged) |
| CoC / open | 159.7 | 159.6 | **−0.1% ± 0.2%** (100% engaged) |
| GATT / SDC | 161.2 | 179.1 | +11.1% ± 0.2% |
| CoC / SDC | 155.3 | 179.0 | +15.2% ± 0.3% |

Open-vs-SDC parity (FSU off): GATT −2.2% ± 0.2%, CoC +2.8% ± 0.2%.

**(R2) CoC/open FSU vs connection interval, counterbalanced** (`coc-fsu-interval-20260908`): flat at
every interval — 15 ms −0.2%, 25 ms −0.0%, 37.5 ms +0.7%, 50 ms −0.0%, and 50 ms/244 B −0.3% (all
±≤1.0%, all ~160–161 KB/s, 100% engaged). **The FSU gain does not reappear at any interval or SDU.**

**(R3) `EVENTFILL_DIAG` throttle hypothesis — refuted** (`coc-fsu-rootcause-20260908/diag-refutation.txt`),
50 ms/244 B: DIAG=y off 158.3 / on 157.5; DIAG=n off 158.1 / on 157.3. FSU −0.5% either way; measured
throttle **+0.2%** (Kconfig help claims ~20%). Occupancy ~33 pkts/ev regardless of FSU.

**(R4) Exact original binaries reflashed today** (`coc-f52.hex`/`coc-f150.hex`, v4.4.1, 50 ms/244 B):
off ~154, on ~156 → **≈+1%**; occ ~33. (Original session: off 131.3, on 150.2, **+14.4%**, occ 28→34.)

**(R5) Bit-faithful full original stack** (v4.4.1 central hex **+** v4.4.1 rebuilt sink): off ~156,
on ~156 → **≈0%**; occ ~32–33.

## Hypothesis-elimination scorecard
| Hypothesis | Verdict | Evidence |
|---|---|---|
| Ambient-RF-day (dismissive) | reframed, not dismissive | it is a real "below-ceiling session" mechanism (below) |
| `BT_CTLR_FSU_EVENTFILL_DIAG` hot-path throttle | **refuted** | R3; exact original binary (which contains the unconditional diag) shows 0% throttle today (R4) |
| Central controller version (v4.4.1 vs v4.4.2) | **refuted** | R4 — the v4.4.1 binary itself runs at ceiling today |
| Config (SDU / interval / FSU floor) | **refuted** | R2; exact original config → ≈0% (R4) |
| Sink controller version (v4.4.1 vs v4.4.2) | **refuted** | R5 — faithful v4.4.1 sink → ceiling, occ 33 |
| **Below-ceiling session / physical operating conditions** | **root cause** | by elimination + the occupancy fingerprint |

## Root cause and mechanism
The +14.4% required event occupancy **28** pkts/ev (below the ~34-pair airtime maximum at 50 ms). The
*identical software* fills to occ **~33** today (at the ceiling). Fewer transmits-per-event with
byte-identical firmware ⇒ the sender was **starved by conditions external to the software** — the
physical/RF operating conditions of that session (documented reproducible below-ceiling levers:
distance/RF and sink buffer/credit depth; not the sink firmware revision, whose source is sha256-
identical across sessions). FSU shrinks the inter-frame gap; that raises goodput **only** when the gap
is the binding constraint on filling the event (occupancy below the airtime max, with data queued). At
the clean-bench ceiling the event is already full for reasons other than the gap, so the freed time is
unused → 0%. This is why below-ceiling sessions (occ 28 / 131 KB/s) showed +9–24% and every at-ceiling
measurement shows ~0%.

## Implications for reported numbers
- **CoC one-way FSU:** reproducible value is **≈0% at the ceiling**. The +9% / +14.4% / +24% figures are
  below-ceiling session artifacts and should not be presented as reproducible one-way CoC gains.
- **GATT one-way FSU +20%:** at-ceiling, reproduced (R1) — robust. This is the load-bearing FSU result.
- **Open-vs-SDC (CoC):** a prior "open +20% ahead at 12.5 ms (170 vs 142)" is *reversed* at the ceiling
  (R1: SDC CoC 179 vs open CoC 159.6, SDC ~+12% ahead); the 142 was a below-ceiling SDC session.
  Counterbalancing (ABBA) cancels drift but **cannot** cancel a whole-session below-ceiling offset —
  do not rely on ABBA alone to validate a cross-stack absolute.
- **Open-vs-SDC one-way parity** and **SDC FSU +11–15%**: reproduced (R1) — robust.
- **Duplex FSU** (+11.9% / +12%) and **CoC-duplex ~181 KB/s**: separately retracted (echo-rig artifact /
  direction-imbalance), independent of this analysis.

## Remaining uncertainties (ranked by how much each would change the conclusion)
Stated adversarially — an unqualified benchmark number is the red flag. Ordered most-conclusion-
changing first; several of these could *invert* a headline.

**U1 — Our own "at-ceiling" ground truth is near-single-session. This is the same trap we just
diagnosed.** The n=60 campaign's ±0.1% CIs are *within-session* precision; they do NOT bound between-day
variance — exactly the offset that produced the August error. CoC ≈0% is replicated across two days
(campaign 09-07 + interval sweep/replays 09-08). **GATT +20% — our load-bearing "robust" headline — is
essentially one day (09-07).** If 09-07 was a GATT-gap-bound session, a different clean day where GATT
already sits at its ceiling could show GATT FSU flat too. We are trusting GATT +20% on the same evidence
class (one session, tight internal CI, ABBA-clean) that fooled us on CoC. **Highest risk of being wrong.**

**U2 — The conclusion may be *inverted* for the actual deployment regime.** "CoC one-way FSU ≈0%" is true
*at the clean-bench ceiling*. The deployment (Wi-Fi-failover safety link, shipyard: congested/ranged/
degraded RF) is a chronically **below-ceiling** regime — exactly where we *did* see +9–24%. So "FSU
doesn't help CoC" may be backwards for the customer's operating point. We have **zero** degraded-RF FSU
data, and it is likely the most decision-relevant cell.

**U3 — We trust the August 131/+14.4% as a *valid* measurement.** The "below-ceiling session" story
assumes 131 was a real operating point. If it was itself flawed (board spacing, USB/cable, a different
analyzer window, thermal), there was no below-ceiling session — just a bad number — and the mechanism is
over-built on noise. Our only corroboration (occ 28) is from that same session. We never reproduced 131
under *any* condition today, so we cannot show the firmware is even *capable* of it — only that it
doesn't happen now.

**U4 — "At-ceiling ⇒ FSU can't help" may be a benchmark-*sink* artifact, not a link truth.**
`mean_pkts/ev` is the *central's* transmit count. If the central is gated by *credit return from the
sink*, then occ ~33 / ~160 reflects the sink's consume/credit rate, not air capacity — FSU could be
adding air capacity the rig's sink can't accept, showing 0% at the sink. We asserted the ~160 is
*airtime*-bound; we did not prove it isn't *sink/credit*-bound (and the docs repeatedly name sink
buffer/credit depth as a below-ceiling lever). If so, a different peer could see the CoC gain.

**U5 — The mechanism is a well-supported hypothesis, not proven physics.** "FSU converts iff gap-bound
with headroom" fits occupancy + throughput, but we did not independently verify what `mean_pkts/ev`
counts (successful TX vs attempts incl. retransmits) nor re-derive the "~34-pair ceiling" (that model may
itself have been measured with the diagnostic on). The *conclusion* (CoC flat at ceiling) is solid; the
*explanation* is inferred.

**U6 — Method residuals.** (a) The "bit-faithful v4.4.1 sink" (R5) came from a `git checkout fsu-m0` +
`west update`; the in-tree BT controller downgraded, but I did not verify the west **modules** (nrfx/HAL)
downgraded to v4.4.1 revs. (b) `analyze.py` was reorganized between August and today — if its
slope/windowing changed, August-131 vs today-154 are not strictly apples-to-apples. (c) The reversed
open-vs-SDC point (R1) is at 15 ms; the original claim was 12.5 ms — direction contradicted, exact
magnitude not re-established. (d) SDC FSU engagement is inferred from the delta (no `spacing=52` token on
SDC); on-air SDC confirmation pending.

## Recommended tests to increase confidence (each maps to an uncertainty above)
**Execution priority — run T-A (degraded-RF) FIRST.** Rationale: (i) it is the test that can *invert* a
headline, not just confirm one; (ii) it is immediately actionable (a physical board move) whereas true
multi-day replication needs calendar time; (iii) it hits U2+U4+U5 at once and is the cleanest direct test
of the mechanism; (iv) it probes the *actual deployment regime* (degraded/ranged RF), not the clean bench.
T-B (multi-day) runs as a background cadence starting with one session now.

- **T-A — Degraded-RF / ranged CoC-FSU sweep** *(→ U2 inversion, U4 airtime-vs-sink-bound, U5 mechanism)*.
  Graded distance/obstruction — **10 cm → ~2 ft → behind 1 wall → behind 2 walls / far** — and at each
  position, CoC one-way FSU on/off, ABBA (≥4 reps), capturing **throughput + occupancy (`mean_pkts/ev`,
  via the confirmed non-throttling `EVENTFILL_DIAG` build) + `spacing=52` + a link-quality signal
  (retransmit/CRC)**. **Discriminating outcomes:**
  - baseline drops **and FSU converts** (gain returns) → mechanism confirmed AND FSU is valuable in the
    deployment regime — the "0%" headline must be scoped to clean-bench.
  - baseline drops but **FSU stays ~0%** → the freed airtime is eaten by retransmits (consistent with the
    existing Test-0 @2 ft ~122 KB/s / FSU ~0% observation, `coc-technical-overview.md §4`) → below-ceiling
    *by RF* is NOT where FSU helps → CoC FSU is a no-op in the real regime too. **Key nuance: "below
    ceiling" is necessary but not sufficient — FSU needs the event gap-STARVED (clean, queued data), not
    retransmit-limited.** August's 131 was a clean-starved below-ceiling point; degraded RF may not be.
- **T-B — Multi-day / independent-session replication of GATT +20% and CoC ≈0%** *(→ U1, the top *risk*)*.
  Repeat the counterbalanced campaign on ≥2 separate days / RF conditions; report between-session spread,
  not just within-session CI — this is what tells us whether our *own* headline is session-robust or
  one-day luck. Note: highest-*risk* uncertainty, but inherently slow (needs real days), so it runs in the
  background while T-A gives immediate signal. Bank session #1 now.
- **T-C — Airtime- vs sink/credit-bound test at ~160** *(→ U4/U5)*. Vary sink RX buffer/credit depth and
  MPS, and read `mean_pkts/ev` semantics directly from `lll_conn.c isr_done()`. If a deeper/faster sink
  lifts the ceiling, ~160 is sink-bound and the "airtime ceiling" mechanism is mis-stated.
- **T-D — Reproduce-or-invalidate the August 131** *(→ U3)*. Re-run the exact archived config repeatedly
  and at varied distance until we either hit a genuine below-ceiling point (validating the session story)
  or conclude 131 was an invalid measurement. (Overlaps T-A's near-field points.)
- **T-E — Cross-check `analyze.py` continuity** *(→ U6b)*: re-analyze an August raw capture with today's
  analyzer; confirm the KB/s method is unchanged, else re-baseline the comparison.
- **T-F — On-air SDC FSU confirmation** *(→ U6d)* with the nRF52 sniffer, to put SDC FSU on the same
  footing as the accepted 1M/2M open-controller on-air results.

**Bottom line for a reviewer:** we corrected one over-claim (August +14.4%) and must not silently install
new ones. **GATT +20% and "CoC one-way FSU ≈0%" should be treated as provisional single-regime results
until at least T-A and T-B are run.** The robust, defensible statements today are the *mechanism-level*
ones (FSU converts only when gap-bound with headroom; verified on-air 150→52 µs) and the *relative* ones
measured same-session (open≈SDC parity), not the absolute per-transport FSU percentages.

## Prior art & novelty (external literature check, 2026-09-08)
Method + bounds: web search (US-locale; Google Scholar index, the SIG-login Core 6.0 spec text, private
repos, and unindexed 2026 papers were out of reach — every "none found" is bounded by that). Split into
what is ours vs. known:

**Known theory we re-applied (NOT contributions):**
- The throughput model — `throughput = PDUs/event × useful-payload / interval`, tIFS as fixed per-packet
  overhead — is textbook. Silicon Labs states it explicitly (`8×(251−4−3)/12500 µs = 156,160 B/s`,
  docs.silabs.com/bluetooth throughput); same in Novel Bits, Punch Through part 4, Interrupt/Memfault,
  Argenox.
- "FSU/shorter tIFS raises throughput" *directionally* — stated qualitatively by the Bluetooth SIG 6.0
  overview and every vendor (Ezurio, Symmetry, Argenox), no numbers.
- "Link-layer gains require a packed event / a fed TX pipe, else airtime is wasted" — Punch Through
  folklore (ble-att-mtu-throughput, throughput-part-4). This is the *mechanism* behind our regime result.
- "Counterbalancing cancels order/linear-trend but not a whole-session confound" — general
  experimental-design knowledge.

**A single FSU throughput number is already public — so we must NOT claim "first FSU throughput
measurement":** Nordic's `sdk-nrf` Bluetooth throughput sample README prints a live run with FSU active
("Frame space updated … 65 us", ~1504 kbps) — but single-config, no baseline, no delta, no methodology.
(github.com/nrfconnect/sdk-nrf …/samples/bluetooth/throughput/README.rst)

**Candidate-novel (no public prior work found; bounded by the search caveats):**
- **[strongest] The transport-/regime-dependence, measured:** FSU ≈ +20% on GATT but ≈ 0% on one-way
  L2CAP CoC *at the ceiling*, with CoC gaining only *below* ceiling. No source ties FSU benefit to
  transport or to a packed-vs-below-ceiling regime with numbers.
- A comparative, transport-differentiated FSU throughput characterization *with* baselines, deltas, and
  methodology (vs. Nordic's single console number).
- The BLE-benchmarking pitfall as a documented caution: a below-ceiling *session* inflates the FSU delta,
  and ABBA counterbalancing does not protect against it.
- The magnitudes on real nRF54L silicon (empirical confirmation the packing corollary binds this way).

**Recommended claim wording (for the artifact and any external write-up):** lead with the
transport/regime divergence as the headline; call it *"the first comparative, transport-differentiated
throughput characterization of FSU"* — **not** "the first public FSU throughput measurement" (Nordic's
sample already prints one, and a reviewer will know it). Present the throughput model as re-applied known
theory, and the pitfall as a BLE-specific methodological caution, not a new statistical principle. The
"2005 patent asserting the latency problem" could not be confirmed — locate the exact number or drop it.

## A note for upstream (Zephyr controller)
`Kconfig.ll_sw_split` help for `BT_CTLR_FSU_EVENTFILL_DIAG` asserts the diagnostic "throttles high-PDU
CoC ~20% (~170→130 KB/s)". Hardware (R3) shows ~0% throttle; the "170→130" almost certainly encodes the
same below-ceiling session as if it were an instrument cost. Recommend softening that help text.

## Reproduce
Open controller = `github.com/teleop-bench/zephyr`, pin the immutable commit `8f44a3d7a3f`
(`v4.4.1-16-g8f44a3d7a3f`). Build `apps/coc/coc-central` + `apps/coc/coc-sink` with `open-fsu.conf`
(FSU on) / `open-nofsu.conf` (off), `-DCONFIG_APP_SDU_SIZE={244,480}`, `-DCONFIG_APP_CONN_INT_UNITS={12,
20,30,40}`. Capture from boot; confirm `Q3FSU-DONE … spacing=52`. Compare the FSU on/off delta **against
the off-arm's distance from the ~160 KB/s ceiling** — a delta is only meaningful with that baseline.
