# Open-controller FSU benchmark — problem statement & proposal

> **Historical planning record.** Kept as written for provenance; its status notes and numbers may
> be superseded or retracted. For current results and their status, see
> [EVIDENCE-INDEX](../EVIDENCE-INDEX.md) and [LESSONS](../LESSONS.md).

*2026-08-12. Scope: obtain a clean, chart-grade Frame Space Update (FSU) throughput,
duplex, and latency dividend on the **open** Zephyr controller (branch `fsu-m0`, HEAD
9999e040), plus the **optimal FSU spacing**, on nRF54L15. Reference arm: the proprietary
SDC, already benchmarked. This document states what is measured, what is blocked and why,
and the experiment that would close it. No new claim is made here.*

---

## 1. Bottom line

The FSU on-air gap reduction is **directly proven at 1M** (tIFS 150→100 µs, ABBA-confirmed,
`fsu-q3a`). Its **throughput/duplex dividend is not yet identifiable** on this rig at the
realistic operating point, and has been resolved only in a non-realistic regime:

- **2M / 7.5 ms:** throughput **−0.98%** [−11.05, +9.09] and duplex **−2.33%** [−6.07, +1.41]
  — both **null**, **non-identifying under an observed ceiling**. This is *consistent with
  masking*, but does **not** establish that the 2M / 52 µs saving was physically present and
  hidden: the direct physical proof is **1M-only**; **physical application at 2M remains to be
  verified** (`debug-evidence/fsu-q3a-20260812/RESULTS.md`).
- **1M / 53.75 ms (quantization-favorable):** throughput **+5.72%** [+1.13, +10.31] —
  the one positive open-controller number, **indirectly measured** (quantization method), with
  separate on-air corroboration of the gap reduction.
- **Latency:** **no reproducible main effect** at the tested 2M point within the preregistered
  ±150 µs margin (+28 µs [−62, +119]); an isolated soak-run anomaly leaves rare/stateful
  effects unresolved.

The reference SDC arm resolved **+15.0%** one-way and **+14.2%** duplex at **2M / 50 ms** —
a regime the open controller has **never been run at**. That is the missing experiment.

> **Conventions.** Throughput is **receiver-delivered goodput** unless stated; "sender-accepted"
> is called out where it differs. Rates are **KB/s** throughout, matching the charts — the
> benchmark's historical calculation **divides bytes by 1024** (i.e. 1 KB = 1024 B). Every
> campaign binds to archived **flashed HEX/ELF + resolved `.config` + controller source hash**.

---

## 2. Goal

Produce, for the customer report, an apples-to-apples **open-controller** FSU characterization:
1. **Throughput** dividend (FSU off vs on), one-way.
2. **Duplex** dividend (simultaneous two-way aggregate).
3. **Latency** effect (expected null; confirm across the spacing sweep).
4. **Optimal FSU spacing** — the value that maximizes throughput before the gain plateaus or
   reliability degrades.

…each with the same discipline as the SDC arm: paired within-session ABBA, HCI-verified
spacing, and (the formal physical gate) an on-air spacing capture.

---

## 3. Current benchmark state

### 3.1 Reference arm — proprietary SDC (already charted)
| Benchmark | Regime | Result |
|---|---|---|
| Throughput one-way | 2M / 50 ms | **+15.0%** [+12.4, +17.7], ~153→178 KB/s, n=6 pairs |
| Duplex aggregate | 2M / 50 ms | **+14.2%** [+12.4, +16.1], ~175→200 KB/s, n=6 pairs |
| Throughput/latency | 2M / 7.5 ms | ≤1% delta (interval/idle-bound null) |

*Status: strong provisional; on-air spacing-during-throughput capture pending.*

### 3.2 Test arm — open Zephyr `fsu-m0` controller (measured, mostly null at 2M)
| Cell | Regime | Result | Verdict |
|---|---|---|---|
| Throughput A/B (`pay2m`) | 2M / 7.5 ms | **−0.98%** [−11.05, +9.09] | null — non-identifying¹ |
| Duplex (`dx2m`) | 2M / 7.5 ms | **−2.33%** [−6.07, +1.41] | null — non-identifying¹ |
| Latency A/B (`latab`) | short interval, 52 µs | **+28 µs** [−62, +119] | null (expected) |
| Throughput (`1m43s`) | 1M / 53.75 ms | **+5.72%** [+1.13, +10.31] | **positive** (indirect) |
| Throughput (`1m30s`) | 1M / 30 ms | −0.55% | flat |
| Spacing negotiation (`sweep`) | — | 100/70/52 µs all SELECT, 0 disc | done |
| On-air tIFS (`fsu-q3a`) | 1M | 150→100 µs, ABBA-confirmed | **mechanism proven** |

| **2M/50 ms preflight** (`fsu-50ms-preflight`) | 2M / 50 ms, MTU-corrected sink-only | **~128 KB/s** | config-gate PASS, **saturation-gate FAIL** (~77% of model; residual cause OPEN) |

Evidence: `debug-evidence/fsu-m0-20260807/` (pay2m, dx2m, latab, 1m43s, sweep, phys, configs,
analyzers), `debug-evidence/fsu-q3a-20260812/` (on-air proof), and
`debug-evidence/fsu-50ms-preflight-20260812/` (the 50 ms preflight cycle: two rig-config
artifacts — peripheral **echo** and **MTU 23** — found and fixed; the corrected **MTU-247
sink-only** run reaches **~128 KB/s** (~77% of the ~167 model), event occupancy ~28–35/event,
no loss, `fmd_arm=0` — **saturation-gate FAIL (~77% of model); residual cause OPEN** (pool 48≈64 and log cadence both ruled out; host ATT-refill a plausible unconfirmed hypothesis)).

---

## 4. The problem — why the open 2M dividend reads null

At **2M / 7.5 ms** the FSU off/on delta is **non-identifying**: the CI spans zero. This is
*consistent with* the ~100–196 µs/pair saving (150→100 proven; 150→52 target) being real but below the measurement's resolution —
**but that reading is inferred, not proven.** Three **candidate** limiters were **observed**;
none has been shown to be the **binding** constraint (that requires the saturation preflight
in §6.6):

1. **Few pairs per event (observed).** A 7.5 ms event carried **~5 packet-pairs** in every
   arm — the same integer quantization bin; a ~100–196 µs/pair saving need not add a 6th pair.
2. **TX-credit ceiling (candidate, not proven binding).** `CONFIG_BT_BUF_ACL_TX_COUNT` was
   **10**. That the 10-deep pool was the *binding* limit is **not established** — it is one of
   several candidates the preflight must discriminate (a pure-capacity control is the test).
3. **Pump ceiling ~157 KB/s (observed).** The CoC blast topped out at an instrument ceiling
   of undetermined location (host-pump attribution was retracted), independent of spacing.

So the honest statement is: the open controller's on-air gap reduction is proven **at 1M**
(`fsu-q3a`); at 2M the throughput delta is **non-identifying under an observed ceiling**, and
**physical application at 2M has not been directly verified** (no on-air 2M capture exists).
The **+5.72% / 1M / 53.75 ms** result is the one regime where a long event (~21–22 pairs)
carried the saving across a quantization boundary — indirectly.

The SDC arm measured at **2M / 50 ms events** (~35 pairs/event) with a pump that fills the long
event; the open controller was **never run in that regime** — every open 2M cell is 7.5 ms with
a 10-deep pool. Note the "realistic 7.5 ms" label is the **safety tier's low-latency operating
point**, *not* the regime FSU was accepted at: the accepted Q3 FSU cells ultimately ran at
**50 ms** after the peripheral's auto parameter-update.

---

## 5. What is established vs open

**Established (open controller):**
- FSU's on-air gap reduction is **directly proven at 1M** — tIFS 150→100 µs, ABBA-confirmed.
- **No reproducible latency main effect** at the tested 2M point within the ±150 µs margin
  (+28 µs); an isolated soak-run anomaly leaves rare/stateful effects unresolved.
- Spacing **100/70/52 µs all negotiate and SELECT** cleanly, 0 disconnects.
- At **2M / 7.5 ms**, the FSU throughput/duplex delta is **non-identifying** on this rig.

**Open (the gap):**
- Whether the ~100–196 µs/pair saving **applies at 2M physically** (no on-air 2M capture exists).
- Whether a **positive open-FSU throughput/duplex dividend** is **resolvable** at 2M / 50 ms
  with a deep pool **once the pump is proven saturated** — never measured.
- The **optimal spacing** for throughput (a frozen multi-arm sweep at a resolvable regime).
- A **qualified on-air 2M spacing capture during the throughput run** (the formal physical
  gate; two prior nRF-sniffer attempts were invalid — the observer needs a bounded 2M
  live-link qualification first, §6.6).

¹ **"Non-identifying"** = the paired off/on interval spans zero; consistent with masking but
not proof the saving was present-and-hidden.

---

## 6. Proposal

**Order of operations:** the saturation preflight (§6.6) must **pass first**. Do not run the
off/on cells until it does — otherwise a fresh ceiling masks the payoff exactly like the old one.

### 6.1 Primary — 2M / 50 ms, deep-pool, one frozen 4-treatment spacing sweep
Rig: the `z54-lat` FSU family (already carries `bt_conn_le_frame_space_update()` +
`APP_FSU_MIN/MAX_US`), reconfigured toward a resolvable, airtime-bound regime:
- **2M PHY, 50 ms interval.** Note a 50 ms interval does **not** guarantee a 50 ms-filled event
  or ~35 pairs/event — **measure actual pairs/event** and require it to be well above the
  ~5-pair quantization floor before the delta is trusted.
- **Deep TX pool with a depth-headroom test.** **Start at 48/64** — 24 is insufficient by the
  frame model (a pool that recycles completions only *after* the event caps at ~24 × 244 B ×
  20 events/s ≈ **114 KB/s**), memory-validated. Run **48 vs 64** and require **deeper no
  longer changes delivered goodput**.
- **FORCE_MD is coupled to the pool depth — instrument or decouple.** FORCE_MD's arm threshold
  is tied to `BT_BUF_ACL_TX_COUNT` (a 24-deep pool may need ~23 transactions to arm — possibly
  above the event's natural stopping point), so **merely enabling it may do nothing**. Add
  **FORCE_MD arm/use counters** and either decouple the threshold or size the pool so it arms.
  If forced event-fill is required, label the result **"open controller + forced event-fill"**
  (a non-standard scheduler aid, held constant across all FSU arms).
- **Pre-run: a small factorial discriminator** {queue-only, FORCE_MD-only, both}, FSU held off,
  attributes the underfill before any FSU cell (see the 2M/50 ms preflight archive below).
- **Direction: central→peripheral (downlink) only** — the stable path. **The peripheral→central
  (uplink) CoC path is BLOCKED**: it carries the fresh-central-reboot wedge (see the uplink
  diagnostic). Duplex (§6.4) is therefore blocked until that path is qualified or the limitation
  is explicitly built into the design.
- **Metric: receiver-delivered goodput** (KB/s), reported separately from sender-accepted.
- **Parameter-update safety (not "pinning").** Either **(a)** disable automatic parameter
  updates and **establish the connection at 50 ms from the start**, or **(b)** request FSU only
  **after the final `le_param_updated`** and **reject any later update** (an update wipes the
  negotiated tIFS via `ull_conn_update_parameters`). Assert per run: correct SELECTED value and
  **no later update**.
- **Controls.** The FSU-**off** arm is **no request (`APP_FSU_MAX_US = 0`)** — the 150→150 no-op
  path emits **no HCI completion** in this implementation, so verify "off" by **configuration +
  on-air/on-chip baseline**, not by a completion event. Require an HCI FSU-completion only for
  the **reduced** arms (100/70/52).

**One frozen design (not separate ABBA pairs).** Run all four treatments **{150, 100, 70, 52}**
in a **single balanced-order block** (e.g. a Latin-square / counterbalanced 4-arm sequence),
**≥6 reset-isolated blocks** (the prior campaign's replication; do not drop to 4 without
justification), boards reset per cell, one host boot per block. Analyze with
**multiplicity-aware simultaneous intervals** across the four arms — *not* separate pairwise
comparisons followed by picking the largest (that invites drift + winner's curse).

### 6.2 Secondary — realistic-cadence confirmation
Repeat **150 vs 52** at **2M / 7.5 ms** (deep pool, same downlink path). Expected: non-identifying
— **confirms** the existing `pay2m` result at the safety-tier operating point.

### 6.3 Latency across the sweep
At a short interval, A/B each spacing (150 vs 100/70/52). Expected: no reproducible main effect
within the ±150 µs margin — **confirms** spacing does not move latency (interval is the lever).
State it as a bounded equivalence, not "no effect."

### 6.4 Duplex — BLOCKED
Duplex needs the peripheral→central (uplink) path, which is not yet qualified (the reboot wedge).
**Do not run** until the uplink path is qualified, or state the uplink limitation as a design
constraint and measure only the qualified direction.

### 6.5 Optimal FSU spacing — pre-registered definition
Defined **before** the run: the **least aggressive** spacing that is **statistically within a
fixed margin (pre-set, e.g. 1 percentage point) of the best** arm, subject to **0 disconnects /
0 rejects / stable negotiation**. Report the full throughput-vs-spacing curve (150→100→70→52)
with simultaneous intervals; the knee is read off that pre-registered rule, not chosen post-hoc.

### 6.6 Saturation preflight (MUST pass before §6.1) — from `fsu-throughput-plan.md`
**Status: RUN 2026-08-12 → NOT YET PASSED (two rig-config artifacts fixed; near-saturation).**
Three iterations, all controller-clean: (1) periph **echo** left on → invalid (ping-pong);
(2) **MTU 23** capped writes to 20 B → invalid (the "6.6× gap" was 20-byte writes, no loss);
(3) corrected **sink-only + MTU 247, pool 48** → **~125–130 KB/s**, event occupancy ~28–35/event
(model ~35), **bytes = callbacks × 244**, **`fmd_arm=0`**. That is **~77% of the ~167 KB/s @150 µs
model** and under the ~240 headroom target — a **configuration-gate PASS but saturation-gate FAIL**.
The **residual ~22% cause is OPEN**: host ATT-refill (ATT `K_FOREVER` allocs; `enomem=0` ≠ spare
capacity) is a **plausible but unconfirmed** hypothesis — ad-hoc pool (48≈64) and cadence (1/5/10 s)
tests did **not** confirm it. The controller is not exonerated either. Runtime effective-DLE was not
logged (gap). The FSU sweep is **not** run. **Next: a bounded FSU-off CoC saturation preflight** —
an *independent* pump architecture that **may** avoid the GATT ceiling (not a claim that GATT is
proven host-bound); ≥2 reset-isolated reps, CoC-specific airtime model, cumulative-byte deltas.
Archive: `debug-evidence/fsu-50ms-preflight-20260812/`.

A KB/s number alone proves nothing. Before any off/on comparison, require **positive evidence
the pump is radio-saturated**, all of:
- **Model-tracking goodput.** Delivered rate near the frame model: **≈167 KB/s at 150 µs**,
  **≈199 KB/s at 52 µs** (before overhead) — recompute from the exact final frame mix. A bare
  "150+ KB/s" is *not* sufficient; the independent **pump-headroom** target (≥20% over predicted)
  approaches **~240 KB/s**.
- **Event occupancy near the model** — measured **TX packets/event ≈ 35 (150 µs) / 41–42 (52 µs)**,
  not inferred from completion gaps.
- **Sustained non-empty controller queue** (queue depth at event boundaries > 0) + **Number-of-
  Completed-Packets** evidence that the controller, not the app, is the bound.
- **CPU-idle headroom** on the sender.
- A **pure-capacity control** response (a change that should raise capacity does).
- **HCI-verified SELECTED spacing on the reduced arms (100/70/52).** The no-request **150 µs
  control emits no completion event** — verify it by **configuration + on-air/on-chip baseline**,
  not by an HCI completion.

Physical gate (separate track): a **qualified on-air 2M spacing capture** during a throughput
run — preceded by a **bounded Q2-2M live-link qualification** of the observer (it is qualified
for live 1M and synthetic 2M, **not** yet a live 2M throughput connection; full Core-6 decoding
is not required for gap timing).

---

## 7. Risks & fallback interpretations

- **The deep-pool pump may still not saturate 50 ms events** (credit-refill batching, CPU, or
  a fresh ceiling). Mitigation: the pump-headroom gate detects it *before* the off/on read.
- **50 ms may introduce its own effects** (refill cadence). Counterbalanced ABBA + within-run
  completion-gap steps (the −102/−103 µs signature seen at 1M) discriminate.
- **If it stays null:** the honest finding is *"the open controller reduces on-air spacing
  (proven) but its throughput dividend is not resolvable on this rig at 2M; the clean positive
  is +5.72% at 1M/53.75 ms."* That is a legitimate answer, not a failure — and the SDC arm
  already shows the feature is worth ~+15% where the instrument can see it.

---

## 8. Deliverables & effort

- Open-FSU **throughput** bar(s) (2M/50 ms) + the **7.5 ms null** confirmation → fills the
  missing rows in the throughput and duplex charts.
- **Optimal-spacing** curve (150/100/70/52) + stated knee.
- **Latency-flat** confirmation across spacing.
- Updated charts (open-FSU rows only if resolved; otherwise the honest "non-identifying" label)
  + archived evidence: paired logs, resolved `.config`, analyzers, **SHA256SUMS**, and — bound to
  each campaign — the **flashed HEX/ELF** and the **controller-source hash** (`fsu-m0` HEAD).

**Effort:** the saturation preflight + primary sweep + confirmation ≈ half a day of reset-isolated
bench runs plus build; the on-air 2M capture (incl. the observer 2M qualification) is a separate,
instrument-gated task. **Gate:** do not start §6.1 until §6.6's preflight passes.
