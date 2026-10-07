# L2CAP CoC vs GATT for BLE teleop links — technical statement for external review

**Scope.** What we learned measuring Bluetooth LE **L2CAP Connection-oriented Channels (CoC)**
against **GATT** (notify/write) as the transport for a robotics teleoperation Wi-Fi-failover
safety/control link. Hardware: 2× nRF54L15-DK, 2M PHY. Stacks: open Zephyr `ll_sw_split`
(+ the `fsu-m0` FSU patch series) and Nordic SoftDevice Controller (NCS v3.4.0). All numbers
are receiver-delivered goodput, steady-window. **Units: KB/s = 1024 B/s** (project convention;
1 KB = 1024 bytes, not the SI 1000-byte kilobyte). **Absolute KB/s are strongly
session- and arrangement-dependent and not stable across sessions** — even some *deltas* (e.g.
the CoC FSU gain) varied between sessions, so treat magnitudes as regime-specific, not portable
constants.
This document is written to be argued with; each claim carries its bound. Where a claim is a
hypothesis rather than a measured result, it says so.

### Primer: SDC vs. the open Zephyr controller (what's the difference?)
Both are BLE **controllers** — the link-layer + radio-timing half of the stack — and both run under
the *same* Zephyr **host** (L2CAP/GATT/GAP). Swapping them changes only the controller, not the app.

| | Open Zephyr `ll_sw_split` (what we benchmark) | Nordic SoftDevice Controller (SDC) |
|---|---|---|
| Source | **Open** — auditable, modifiable | **Closed** precompiled binary blob |
| Silicon | Portable (many vendors) | Nordic only |
| New BLE-spec features | Often experimental / lagging — we patched **FSU** in ourselves | Production-first (FSU, ISO, coex ship qualified earlier) |
| Connection-event-length extension | **No** — packs to the interval via More-Data + ticker preemption | **Yes** — opportunistically extends an event |
| BLE + Wi-Fi coexistence | Reactive 1-wire only (BLE always yields) | **Full MPSL CX** (priority/PTA) — *but see note* |
| Qualification / support | Self-qualify | Nordic-qualified + supported |

**What we measured:** at a single connection, zero load, the two were **near parity in several
matched cells, not across the board** — one-way peak ~166 KB/s both at GATT/50 ms in the August
head-to-head (means 143 vs 145), echo-rig duplex ~181 vs ~180 and idle latency 11.9 ms each in earlier
same-bench runs (`sdc-bench-20260806`); see §2 and the [evidence index](../EVIDENCE-INDEX.md). *(Update
2026-10-05: the August CoC head-to-head cells, including "open +19.7% at CoC/12.5 ms", are quarantined: their sinks'
auto-update moved the link to ~50 ms. A same-session, equal-tuning campaign found open ahead by ~30 KB/s at 7.5 ms
and ~16 KB/s at 15 ms on GATT and CoC, converging from 25 ms; [`oneway-crossstack-20261005`](../../debug-evidence/oneway-crossstack-20261005/README.md).)* The differences bite at the *edges*: many
concurrent links (SDC schedules them better), brand-new features (SDC first), and real Wi-Fi
coexistence (SDC only). For a heartbeat + stop-signal safety link the **open stack is at or near SDC
on the numbers that matter, with full auditability and no vendor lock-in** — a feature, not a compromise, for
a safety-critical channel. Pick **SDC** when you need production ISO/FSU today, Wi-Fi coex, or many
simultaneous connections; pick **open** when auditability, portability, or modifying the controller
(as we did for FSU) matters more.
*Wi-Fi-coex note:* this deployment runs **Wi-Fi on 5 GHz only** (BLE on 2.4 GHz), which **substantially
removes the direct spectral overlap** that SDC's MPSL-CX is for — so coex is a much weaker reason to
pick SDC here. It is **not "moot":** same-device front-end coupling, receiver desense, harmonics/IMD,
shared antennas, and radio-scheduling/resource interactions can still occur and **require validation**
(AFH cannot correct a co-located desense/blocker). Residual 2.4 GHz *airwave* congestion from other
emitters is separate, handled by AFH/retransmit, not coex hardware.

---

## 1. Headline

CoC and GATT achieved **comparable clean-link one-way throughput** in several tested regimes. A
draft of this document reported an apparent "open CoC is RF-fragile — same binary dropped
~170→~131" finding; a file-level audit of our own evidence **retracted it** (§4): the ~170 and
~131 numbers came from **different builds at different intervals** (a 480 B / 12.5–15 ms central
vs a 50 ms-hardcoded one), so no fixed binary ever "dropped," and the ~130 ceiling is a known,
non-RF, architecture-independent open-host limit. **We have no evidence that CoC is more
RF-fragile than GATT.**

For the teleop use case we **lean toward GATT** for the safety/control link on design grounds:
GATT notify makes *freshest-value* control easy (no app-layer credit backpressure to queue stale
commands), it interoperates more broadly (CoC is often absent off-embedded), and it has far fewer
silent config cliffs (§6). RF-robustness is **not** among the reasons — it is neither demonstrated
nor, on current evidence, indicated.

## 2. Throughput at clean range (what CoC *can* do)

At close range / low interference, one-way CoC and GATT are in the same band (~150–180 KB/s
at 2M), each peaking at moderate connection intervals (~7.5–25 ms), both falling off past
~50 ms as fewer events/sec stop being repaid by fuller events. Same-session open-vs-SDC
comparisons were **near-parity on several hardened cells**, but not uniformly. *(Superseded 2026-10-05: the
"CoC at 12.5 ms, open +19.7%" cell is quarantined (sink auto-update moved it to ~50 ms); the same-session,
equal-tuning campaign shows open ahead at 7.5–15 ms and the stacks converging from 25 ms,
[`oneway-crossstack-20261005`](../../debug-evidence/oneway-crossstack-20261005/README.md).)* CoC's
theoretical edge over GATT (no ATT/GATT per-packet overhead) is small in practice because DLE
already amortizes the GATT header (~3 B).

**Bound:** absolutes vary ±10–20% session-to-session/arrangement-to-arrangement; treat the band,
not the point.

## 3. Frame Space Update (FSU) on CoC

> **Superseded numbers — read this first.** The CoC FSU gains below (+9.0% to +23.2%) predate the
> held-FSU campaign. Several of those runs used sinks with GAP auto-update on, which can silently
> revert FSU mid-run ([LESSONS #1](../LESSONS.md)). The current result, with FSU verified held and
> matched arms, is **CoC/open +20.4% ±1.0% (n=15)** and **CoC/SDC +11.6% ±0.8% (n=12)** at
> 15 ms / 480 B ([`coc-fsu-corrected-20260908`](../../debug-evidence/coc-fsu-corrected-20260908/),
> [`sdc-fsu-corrected-20260908`](../../debug-evidence/sdc-fsu-corrected-20260908/)). The "CoC FSU ≈0% at 7.5 ms on
> both stacks" reported here earlier is retracted as a CoC property: the sink returned one credit per segment, and
> with batched returns CoC gains +20.0% (Zephyr) / +25.3% (SDC) at 7.5 ms, like GATT; the batched sweep also moves
> SDC's 15 ms rates (141 → 156 KB/s FSU off, +11.3% → +10.0%) and 25 ms gain (+18.5% → +11.8%)
> ([`coc-credit-policy-20261006`](../../debug-evidence/coc-credit-policy-20261006/README.md),
> [`oneway-coc-batched-20261006`](../../debug-evidence/oneway-coc-batched-20261006/README.md)). The duplex figures below predate the matched-arm GATT duplex run, which
> found no FSU gain at 7.5 or 15 ms and about +10% at 25 ms on both stacks, explained by whole
> exchanges per connection event ([`gatt-duplex-fsu-matched-20261003`](../../debug-evidence/gatt-duplex-fsu-matched-20261003/README.md),
> [`gatt-duplex-fsu-model-20261003`](../../debug-evidence/gatt-duplex-fsu-model-20261003/README.md));
> the model then predicted three new intervals before they were run (12.5 / 22.5 / 35 ms: 0 / +10.6 /
> +6.9%). CoC duplex FSU was measured on 2026-10-03 (see the note in the duplex section below). The text
> below is kept as the historical record.

FSU (BLE 6.0; open-stack controller via the unmerged `fsu-m0` WIP we finished + measured)
converts to CoC throughput **only when the link is airtime-bound with fill headroom**. The
measured open-CoC gains, each **same-interval** (isolating FSU) but from different runs/methods:
- **+14.4% at 50 ms** (drift-cancelled ABBA) — the most rigorous cell;
- **+16.5% at 15 ms** (interval-envelope);
- **+23.2% at 15 ms** in a single-run sweep.

A clean on-bench re-test (2026-08-25, 2× nRF54L15) pinned this down: **one-way +9.0% at
10 cm / 15 ms / 480 B** and **+18.7% at 25 ms / 480 B** (both ABBA drift-cancelled, `spacing=52`
verified: e.g. 15 ms FSU-on 164.0 vs off 150.5; 25 ms FSU-on 192 vs off 162) and **duplex +11.4%
at 10 cm / 25 ms** (reproducing the historical 2 ft +11.9% — the delta transfers across distance
even as absolutes rise close-in). The +9→+18.7% climb from 15→25 ms confirms the mechanism below.
So the gain is **real but
regime-dependent**: it scales with **how airtime/packing-bound the event is**, which is why the
historical band runs +9 to +23% across intervals/sessions. Do not read a single number as *the*
FSU gain.

**The key mechanism (resolved 2026-08-25; airtime cause confirmed 2026-08-26, §11.1):** FSU
converts **~0% when the link is distance-degraded**, because RF retransmits at range consume the
airtime that FSU's shorter tIFS would otherwise free — the event is no longer cleanly packed with
good PDUs, so shrinking the inter-packet gap reclaims nothing (e.g. 7.8 good PDU/event at 2 ft vs
~10 at 10 cm, same 15 ms). Conversely, close-in the event **is** tIFS-packed and FSU converts the
gap directly (+18.6% PDU/event at 15 ms; §11.1). Earlier "FSU didn't convert / suppression
unresolved" observations were exactly the degraded regime — **not an FSU bug, and now explained.** FSU application was confirmed by the peer's `frame_space_updated` callback
reporting the negotiated spacing (52 µs) — the *negotiated* frame space, **not an on-air tIFS
measurement** (no passive observer on the 37-channel hopping CoC runs; the CoC duplex link was later
observed on air on one data channel, 2026-10-03, [`duplex-fsu-followups-20261003`](../../debug-evidence/duplex-fsu-followups-20261003/README.md) §3).

A documented (non-exhaustive) search **found no directly comparable published measured
FSU-throughput figure** — only feature descriptions, fixed-150 µs theoretical math, one
*simulated* MATLAB IFS curve, and one hardware writeup that names FSU beside ~1.5 Mbps without
isolating it. So this may be the first public one — stated as *novel to our knowledge*, not a
priority claim. Search method, queries, sources, and limits archived in
`prior-art-search-20260825.md`. **Bound:** FSU here is non-spec/experimental and requires our
patched controller on both ends.

## 4. Central observation: the apparent "CoC RF-collapse" is largely a measurement artifact

An earlier draft of this section reported an **uncontrolled** two-session observation — open CoC
"~170 → ~131 KB/s" while GATT held ~178 — and floated RF as the cause. A file-level audit of our
own committed evidence (2026-08-25) **substantially retracts that**. The "170 → 131 drop" does not
survive scrutiny of what was actually measured:

**The ~170 baseline and the ~131 re-flash are two different operating points, not one binary
degrading.** The "decisive re-flash" used `coc-f52.hex`, whose interval is **hardcoded to 50 ms**
(`central-main.c:228` `BT_LE_CONN_PARAM(40,40,0,400)` "50 ms preflight") and **cannot** run at any
other interval — it does not read the sweep's interval knob. On 2026-08-13 that exact hex measured
**150.2 KB/s FSU-on / 131.3 FSU-off at 50 ms** (`coc-open-fsu-20260813/FINDINGS.md`) — it **never
measured ~170**. The ~170 figures came from a *different* build (480 B SDU, rebuilt at 12.5–15 ms;
`OPTIMAL-ENVELOPE.md`). So the re-flash's **~131 is this binary's own FSU-off number at 50 ms**,
not a degraded 170 — plausibly with FSU simply not arming (the sink FSU-responder config cliff of
§9.4 #1 drops it from the 150 path to the 131 path). `RECONCILIATION.md` had already recorded the
governing fact: **"interval is the driver."** The "same binary 170 → 131" premise has a broken
first link.

**The ~130 ceiling predates the "drop" and is architecture-independent.** On 2026-08-12 — before
any drop narrative — an open **GATT-write** pump measured **~128 KB/s** and an open **CoC** pump
**~121 KB/s**, both at 50 ms, both attributed at the time to an unresolved **host-refill/occupancy ceiling** (later
resolved: at the throughput peak the ceiling is tIFS airtime, §11.1)
(occupancy ~25–28 vs a ~35/event airtime model), explicitly *"architecture-independent"* and *not
RF* (`fsu-50ms-preflight-20260812`, `coc-fsu-preflight-20260812`). Deepening the TX pool later
recovered open-CoC **132.5 → 142.8** at 50 ms (`systematic-sweep-20260814`). So ~130 at long
intervals is a **known, pre-existing open-host-pump limit that also caps GATT** — a buffer/refill
effect, not an RF-vs-transport effect. (Regime note per §11.1: buffer depth only matters at **long**
intervals like 50 ms, where a long event can outrun a shallow pool; at the **12.5–15 ms throughput
peak** deep≡shallow and the ceiling is pure tIFS airtime. Both regimes recover *toward* their
airtime ceiling — ~32 PDU/event at 50 ms, ~10 at 15 ms — never above it.)

**A same-session signal points *away* from broadband RF.** In the identical 2 ft/RF matrix,
**SDC-CoC held ~152–155 while open-CoC sat ~130** (`matrix-2ft-clean.csv`). Broadband RF would
suppress *both* CoC stacks; only the open-stack path was low — pointing at something
open-CoC-specific (build/config/host-refill), not the air.

**What the sink-credit-regression hypothesis is worth (weaker than a prior draft claimed).** The
sink source (`sink-main.c`) and all credit/buffer Kconfig are **byte-identical** to the original
(sha256 match), the credit design (`RX_CREDITS=64`, 1-credit-per-segment) is the **same across all
sessions**, and the fsu-m0 git base differs by **one commit** whose delta is a gated event-fill
diagnostic that does **not** touch `seg_recv`/credit code. What actually changed on the sink was
**config** (the `CONN_INTERVAL_LOW_LATENCY` responder fix) and a possibly-reconstructed `main.c`
— not a credit regression. So this is a minor open item, not a co-leading cause.

**Strongest supportable conclusion.** There is **no established RF-induced CoC collapse** in our
data; the headline was an artifact of comparing a 50 ms binary against a 12.5–15 ms baseline, on
top of a long-standing, non-RF, architecture-independent open-host ceiling. Relative RF robustness
(CoC vs GATT) is **not demonstrated here**.

**Test A (2026-08-25, on-bench) resolved the remaining puzzle — and the reconstructed central is
not broken.** A matched-config rebuild (480 B / 15 ms / FSU-on, all verified on air:
`interval=12→15.00ms`, `spacing=52`, `PHY 2M`, `DLE 251`) was measured on the two nRF54L15-DKs:
- **~2 ft (current room): 121.6 KB/s, 7.8 PDU/connection-event** — distance-degraded (`eagain=0,
  poolfail=0`: credits and app pool never bind; the 7.8 vs ~10 close-in shortfall is RF retransmit
  airtime at range, §11.1), so FSU converts ~0% — the freed tIFS is spent on retransmits, not new PDUs.
- **10 cm: 161.9 KB/s, 10.4 PDU/event** — reproduces the original ~170 operating point (which was
  10–12 PDU/ev) with the *same binary*; 10.4 PDU/event **is** the 15 ms airtime ceiling (§11.1),
  not a sub-airtime refill floor.

So the "reconstructed central tops out at ~135" was **distance in this room, not a build/config
regression** — the same binary lifts 122→162 (+33%) simply by moving 2 ft → 10 cm. `~170 is
reproducible at close range with matched config.`

**Test A′ (2026-08-25) settled the transport question — CoC is NOT more RF-fragile than GATT.** A
paired close-vs-far comparison at matched single-segment payload (244 B / 15 ms / no-FSU / 2M, all
verified on air) on the same two boards:

| 244 B, 1 segment | 10 cm | 2 ft | drop |
|---|---|---|---|
| CoC-244 | ~158 | ~150 | **5%** |
| GATT-244 (Write-Without-Response) | ~156 | ~145 | **7%** |

CoC and GATT lose the **same ~5–7%** with distance — indistinguishable. The transport is not the
variable.

**The real driver is SEGMENTATION, not CoC (measured, replacing the earlier credit-flow
speculation).** Cross-referencing the 480 B runs at 2 ft: **480 B (2 segments) → ~120 KB/s (both
FSU states); 244 B (1 segment) → ~150.** A 480 B SDU spans 2 PDUs and *both* must arrive for
reassembly, so its effective loss is ~2× a single-PDU transfer — that is the entire ~25% drop the
earlier 480 B CoC showed at 2 ft, while 244 B (CoC *or* GATT) held. This is a **payload/config
property (avoidable with ≤MPS single-segment SDUs), not CoC-specific** — a large multi-notification
GATT transfer reassembles the same way. So the correct mechanism is *multi-segment reassembly
amplifies loss*, not *CoC credit-flow is RF-fragile*. (Evidence:
`debug-evidence/coc-vs-gatt-rangetest-20260825/`.)

**To resolve the remaining (non-RF) puzzle:** the actual open question is the reconstructed-vs-
original open-CoC gap at matched interval (~135–142 vs ~170 at 15 ms). Close it by **rebuilding
the original 480 B / 12.5–15 ms central config and confirming FSU actually arms (`spacing=52`)
with per-event occupancy logged** (§10 Test 0). Only if a clean, matched-config rebuild still
falls short — and occupancy shows loss/retransmit rather than host-refill starvation — is a
controlled degraded-RF campaign (`degraded-rf-campaign-plan.md`) warranted. RF is the *last* thing
to reach for here, not the first.

**On the RF-robustness question in the abstract:** a documented (non-exhaustive) search still
found **no published measurement** of CoC throughput under degraded RF, let alone against a GATT
control arm (`prior-art-search-20260825.md`) — every in-the-wild CoC "collapse" is a clean-link
credit/scheduling bug (Zephyr #69975, NimBLE #818), and the RF-throughput studies (Pang et al.
IEEE 2022; Spörk EWSN '20) are transport-blind. So it remains **unstudied territory** — but our
data does **not** fill it; we have no evidence CoC is RF-fragile, only a retracted artifact and an
open build/config gap.

## 5. CoC-duplex: ~181 KB/s in one config; the peripheral-TX stall is INTERMITTENT (DLE=251 trigger RETRACTED)

> **⚠️ RETRACTED — SUPERSEDED BY §11.1 (2026-08-26). Read this first.** The "REAL and reproducible,
> `251 → stalls`" claim below **did not hold on re-test**: at `tx_max=251` the peripheral uplink
> **flowed normally across all four channel-opens tested** (host stages 1:1), and the stall did **not**
> reproduce. The stall is **intermittent**, not a deterministic DLE=251 effect. The confident
> "trigger identified" text below is kept only as the reasoning trail — treat §11.1 as the current
> truth. *(Also note: the "~120 down + ~60 up" duplex split below is the **single-channel CoC-duplex**
> regime where uplink fragments; it is a different config from the symmetric ~152/~152 one-way numbers
> — do not read the ~60 as a persistent uplink asymmetry.)*

**Original (RETRACTED) reconciliation — 2026-08-25.** The trigger was thought to be identified,
reconciling two earlier contradictory readings:
- CoC-duplex reaches **~181 KB/s aggregate** (downlink ~120 + uplink ~60) **when the L2CAP channel
  opens *before* the peripheral's TX data length reaches 251** — the uplink then starts on small
  (27-octet) PDUs and flows (fragmented, ~60 KB/s).
- But the **08-14 peripheral-TX stall REPRODUCES** when the peripheral TX DLE is already **251** as
  the uplink starts: `UP sent` freezes at the pool depth, **`txcred` freezes at 64** (never
  decrements — no segment handed to the controller), uplink = 0. Confirmed in the FSU-armed config
  (which *must* open L2CAP after full DLE, so the uplink starts at DLE=251).
- **Discriminator: the peripheral TX DLE at the moment the uplink starts** — 27 → flows;
  251 → stalls. This reconciles the 08-14 stall (DLE=251) with the earlier re-check flow (which
  opened L2CAP early, DLE still 27, so it dodged the trigger).

**Retraction of the retraction.** An intermediate draft said *"the stall does not reproduce; no
reproducible CoC-duplex bug; cause unrecoverable."* **That was wrong** — the stall IS reproducible,
and the trigger (peripheral TX DLE=251 at channel open) is identified, **partially vindicating the
original 08-14 diagnosis**. The lesson survives in a sharper form: *a "does not reproduce"
conclusion is only as strong as the config you re-tested — the re-check happened to pick the one
config that dodges the trigger.*

*(Resolved 2026-10-03: measured cleanly by discarding the first connection after each central boot and
resetting only the peripheral per rep — open +7.7/+7.5/+15.2%, SDC −0.2/+1.5/+16.2% at 7.5/15/25 ms, no
stalls; [`coc-duplex-fsu-matched-20261003`](../../debug-evidence/coc-duplex-fsu-matched-20261003/README.md).
n=8 re-runs: open 25 ms +12.0% ± 0.5%, SDC 15 ms +3.9% ± 2.4%. On air the open CoC link mixes duplex
events with one-way events (one side sends only empty packets), and the one-way events gain ~20% at every
interval; [`duplex-fsu-followups-20261003`](../../debug-evidence/duplex-fsu-followups-20261003/README.md).
Root cause of the uneven split (2026-10-06): the sink's credit returns wait in its 64-deep controller TX queue
behind its uplink packets; sink `BT_BUF_ACL_TX_COUNT` 20 or 8 gives 1:1 and ~6% more aggregate, so the open CoC
duplex FSU gain is specific to the 64-deep recipe (FSU not re-measured at 20);
[`coc-duplex-credit-trace-20261006`](../../debug-evidence/coc-duplex-credit-trace-20261006/README.md).
Replicated 2026-10-04 (boards further apart, byte-identical firmware): every cell kept its verdict;
[`replication-20261004`](../../debug-evidence/replication-20261004/README.md).)*
*(Superseded 2026-10-06: everything in the note above was measured with two credit leaks in our CoC apps (stale
counters and stale `seg_recv` `rx.credits` sent as initial credits) and a central that opened with 0 initial credits,
which trips a Zephyr host stall: that stall was the reconnect wedge, and the leak produced the ~1:2 split. With the
apps fixed, both stacks split ~1:1 and CoC duplex FSU is Zephyr +0.3 / +0.1 / +23.8% (25 ms later resolved: credit-loop
starvation from 64-deep queues; with 20-deep queues Zephyr +0.2 / +2.5 / +9.6% = SDC, replicated 2026-10-07,
[`coc-duplex-fsu-q20-20261006`](../../debug-evidence/coc-duplex-fsu-q20-20261006/README.md); the original wording was: provisional, its FSU-off
rate is unexplained), SDC +0.0 / +2.8 / +10.2% at 7.5 / 15 / 25 ms;
[`coc-credit-fixes-20261006`](../../debug-evidence/coc-credit-fixes-20261006/README.md),
[`coc-duplex-fsu-fixed-20261006`](../../debug-evidence/coc-duplex-fsu-fixed-20261006/README.md).)*
**Consequence (as of 08-26; SUPERSEDED — see the 2026-10-03 note above. Kept as history: the 08-24 GATT
+11.9% off-arm was later found confounded, and the DLE=251 trigger named below was retracted.)**
CoC-duplex + FSU is blocked on this stack. FSU requires L2CAP to open after
PHY=2M + full DLE (else the request returns `-EACCES`, §gotchas), but opening after DLE=251
re-triggers the uplink stall. So there is **no clean open-stack CoC-duplex FSU number**; the duplex
FSU matrix stays 3-of-4 (GATT duplex +11.9% clean; CoC duplex ~181 works only FSU-off / early-open;
FSU-on stalls the uplink). Root-causing the **DLE=251 peripheral-TX drain stall** (host/controller
TX path — `l2cap_data_pull` / `tx_processor` handing a full-DLE segment) is the genuine open item.
CoC-duplex one-way *downlink* is unaffected and healthy (downlink-only FSU @25 ms = **+18.7%**, §3).

## 6. Engineering hazards CoC exposes (reproducer beware)

CoC is markedly more sensitive to configuration than GATT — several throughput cliffs are
silent (build/run fine, wrong number):
- **SDU size vs MPS.** 480-B SDUs (2 segments) throttle vs 244-B (1 segment): more PDUs,
  segmentation, and more K-frames each consuming a credit (replenished per-frame or batched — see
  below). Single-segment SDUs are the efficient choice.
- **Credit replenishment.** Returning 1 credit per segment generates a credit PDU per segment;
  batching matters under any stress.
- **FSU responder config.** The peripheral needs `CTLR_CONN_INTERVAL_LOW_LATENCY` +
  `EVENT_IFS_LOW_LAT_US=52` or FSU negotiates but never applies (silent 0% "gain").
- **DLE on both ends.** A peripheral that never extends data length pins the reverse path at
  27-octet PDUs.
- **Buffer/credit depth.** Too shallow → the link is buffer-bound below the airtime ceiling and
  FSU can't convert.

GATT exhibits none of these to the same degree — fewer moving parts is itself a robustness
argument for a safety link.

## 7. When CoC is still the right choice

CoC earns its place only when the application genuinely needs **native, symmetric,
back-pressured high-rate streaming between two devices you control end-to-end** (e.g. drop-free
sensor-up + telemetry-down, both saturating). A teleop safety link is **asymmetric** (bulk one
way, tiny control the other) and does not need this; the tiny control/heartbeat traffic is
best served by freshest-value semantics, and CoC's app-layer backpressure works against that.
(Note: "credit-free" applies at the L2CAP-app layer only — HCI-layer credit wedges still exist,
e.g. Nordic DevZone #124402; GATT is not backpressure-immune end-to-end.)

## 8. Recommendation

- **Lead with GATT** for the teleop safety/control link, on design grounds: freshest-value
  control is easy to implement (no app-layer credit backpressure to queue stale commands),
  broader interop (CoC is often absent off-embedded), and fewer silent config cliffs (§6). Pair
  bulk telemetry with app-level completion-pacing. (RF-robustness is **not** a reason — the
  apparent CoC RF-fragility was retracted as a measurement artifact, §4.)
  **Caveat (§11.2): the backpressure→staleness argument is design-reasoned, NOT measured for CoC.**
  Our latency-under-load data (stop-signal RTT that blows the >30 ms budget under a saturated bulk
  stream, fixed by completion-pacing) is **GATT-only**. The matched CoC measurement — stop-signal
  latency while a CoC bulk stream saturates — has not been run, so "CoC's credits make the
  stop-signal stale" is a well-grounded hypothesis, not a measured result. Planned (§10 Test B′).
  *(Since measured, 2026-08-25: §11.2.)*
- **Reserve CoC** for a dedicated own-device high-rate bidirectional backpressured stream, at
  known-good RF, with careful SDU/credit/buffer tuning.
- **Open next step (since done, §10 Test 0):** a small, non-RF confirmatory rebuild (§10 Test 0 dynamic) — rebuild the
  original 480 B / 12.5–15 ms central, confirm FSU arms and occupancy, expect ~170. The RF
  campaign is a contingency only if that leaves a genuine matched-config gap.

## 9. Investigation log — the 2026-08 sessions (and how the RF read was walked back)

This is a **lab notebook**, not a controlled experiment. It documents the elimination process
that *originally* pointed at RF — and the later file-audit (§4, 2026-08-25) that retracted it once
the "170 → 131 same-binary" premise was found to conflate two different operating points. Read it
as the history of a wrong turn corrected, not as support for RF. (Evidence:
`debug-evidence/full-matrix-rerun-20260824/`, `coc-duplex-artifact-20260814/`,
`coc-open-fsu-20260813/` — note the `full-matrix-rerun` `FINDINGS.md` still contains the
un-retracted RF claim; §4 here supersedes it.)

### 9.1 Why we re-ran
Two triggers: (a) a request for a single, internally-consistent throughput matrix on one device
arrangement; (b) discovery that the CoC benchmark apps had only ever lived in ephemeral `/tmp`
and were gone — so the CoC results (incl. the +23% FSU headline) were **not reproducible** from
what was committed, and the doc's linked `REPRODUCE.md` did not exist.

### 9.2 Reconstruction of the CoC apps
Rebuilt buildable `coc-central`/`coc-sink` from committed evidence snapshots (SDC-safe guarded
central `main.c`, sink `main.c`, `prj.conf`, `Kconfig`, FSU configs). Recovered the open FSU
on/off configs; **reconstructed the missing SDC-CoC configs** (`sdc-fsu` recovered,
`sdc-nofsu` derived as in-band MIN=MAX=150). All four variants (open/SDC × central/sink) built
and ran — validated at the data level, not just the build. This makes the benchmark harness
reproducible **going forward**; it does **not** retroactively make the historical decisive
campaign independently reproducible (see Provenance — the original sink hex and that campaign's
raw captures were not preserved).

### 9.3 The arrangement journey (why absolutes kept moving)
The full sweep was run in four device placements before we settled on one — an **investigative
history, not a controlled distance sweep** (placement, orientation, and ambient interference all
varied together and were not isolated). CoC read as the more sensitive transport across these,
GATT the steadier — a pattern, not a measurement. Choosing ~2 ft over touching-adjacent was a
**selection judgment**, not a rejection of bad data: touching-adjacent is a legitimate
best-case *upper bound*, just not a deployable operating point.

| arrangement | open CoC one-way | GATT one-way | verdict |
|---|---|---|---|
| "several feet" (2026-08-13, prior) | **~170** — *480 B / 12.5–15 ms build* | ~175 | airtime-bound, clean |
| separated (this session) | **~128**, FSU noise | ~169 | CoC suppressed |
| touching-adjacent | ~164, FSU **0%**, occ pinned 35 pkt/ev | 197 / duplex **214** | buffer-capped CoC; GATT at ~2M **silicon ceiling** — a best-case upper bound, not deployable |
| **~2 ft (final matrix)** | ~130, FSU noise — *reconstructed central, 15 ms* | 178 / duplex 190 | realistic absolutes; open-CoC low (build/config gap, §4) |

**Do not read this table as "CoC fell with placement."** The rows differ in **build and interval
as well as placement** — the ~170 row is a 480 B / 12.5–15 ms central; the ~130 rows are a
different (50 ms hardcoded, or reconstructed-15 ms) central. Per `RECONCILIATION.md` "interval is
the driver," and per §4 the ~170-vs-~130 comparison is confounded by config, not a clean
placement/RF axis. Touching-adjacent's ~197/214 (~96–98% of the 2M ceiling) is a near-perfect-link
upper bound. Cross-row absolutes are **not** apples-to-apples.

### 9.4 CoC-open hypothesis elimination (the core of the rigor)
Open CoC read ~130 at 2 ft vs the original ~170. Each hypothesis was tested and killed:

| # | hypothesis | test | result |
|---|---|---|---|
| 1 | FSU responder mis-configured | check `spacing`; add `CTLR_CONN_INTERVAL_LOW_LATENCY` to sink | **real bug, fixed** (FSU now applies, `spacing=52`) — but throughput still ~130 |
| 2 | SDU size (480 = 2 segments) | set `SDU_SIZE=244` (1 segment) | still ~130 — ruled out |
| 3 | guarded vs original central `main.c` | build & run the original `central-main.c` | still ~130 — ruled out |
| 4 | our ISR event-fill instrumentation (hot-path histogram in `isr_done`) | Kconfig-gate it, build with it OFF | still ~130 — ruled out (gate kept anyway, §9.6) |
| 5 | an external/runtime factor (RF or other) | flash the committed `coc-f52.hex` (believed to be "the ~170 binary") | ~131 — **but see §9.5: that premise was wrong** |

### 9.5 The re-flash test's premise was false — this is where the RF read collapses
The re-flash was meant to be decisive: *"flash the exact binary that measured ~170; if it still
reads ~131, the binary is exonerated and the environment did it."* The later file audit (§4)
broke the premise. `coc-f52.hex` has its **interval hardcoded to 50 ms**
(`central-main.c:228`, `BT_LE_CONN_PARAM(40,40,…)`) and **cannot run at 12.5–15 ms** — so it
**never produced ~170**. Its own on-record 08-13 numbers are **150.2 (FSU-on) / 131.3 (FSU-off) at
50 ms** (`coc-open-fsu-20260813/FINDINGS.md`); the ~170 came from a *different* 480 B / 12.5–15 ms
build (`OPTIMAL-ENVELOPE.md`). So reading ~131 on the re-flash is **exactly this binary's own
FSU-off number at 50 ms** — most likely FSU didn't arm (hypothesis #1's sink responder cliff),
dropping it from the 150 path to the 131 path. **No environmental change is needed to explain it,
and no "170 → 131 drop" of a fixed binary ever actually occurred.** Hypotheses 1–4 remain validly
eliminated for the *reconstructed* central's ~135–142 at 15 ms (§9.4) — that residual build/config
gap is real and still open — but the RF conclusion this section once carried is **withdrawn**.

### 9.6 Side-finding: our own diagnostics throttled CoC
Hypothesis #4 was wrong as the *2 ft* cause, but it surfaced a real benchmark-hygiene bug: the
FSU event-fill histogram captured in `isr_done` on every connection event **does** measurably
throttle high-PDU-rate CoC (it just wasn't the environment-suppressed 130). It is now gated
behind `CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG` (default off) so throughput builds carry no hot-path
diagnostic — important for any reproducer too.

### 9.7 What the sessions actually taught
- **The "CoC is RF-fragile" read was an artifact of not pinning the operating point.** Interval
  (12.5–15 vs 50 ms), SDU size (480 vs 244), FSU-arming, and buffer depth were all in play across
  sessions; comparing across them produced an apparent "drop" that was really a config difference.
  `RECONCILIATION.md` had already flagged "interval is the driver" — we lost that thread and
  re-derived a physics story from a bookkeeping error.
- **~130 at long intervals is a known, non-RF, architecture-independent open-host ceiling** —
  documented on 08-12 for *both* GATT and CoC pumps, recoverable to ~142 with deeper buffers. Not
  a transport-vs-RF effect.
- **CoC's absolute throughput is regime-specific; so are its deltas.** The FSU gain varied
  session-to-session; treat both absolutes and deltas as operating-point-bound, and always record
  interval + SDU + FSU-arm state alongside any number.
- **Silent config cliffs are real** (§6): a missing responder Kconfig gave a clean-looking 0% FSU;
  always-on ISR diagnostics silently biased CoC. Both would mislead a context-free reproducer.
- **Method lesson (the hard one):** "re-flash the original artifact" only works if you verify the
  artifact's *operating point*, not just its filename. We re-flashed a 50 ms binary believing it
  was the 15 ms one. Validate interval/SDU/`spacing` at the data level **before** drawing a
  conclusion from a re-flash.

## 10. Open testing plan — status after the §4 retraction

The §4 audit already answered the big question on paper: the "RF-fragile CoC" headline was a
measurement artifact (a 50 ms binary compared to a 12.5–15 ms baseline), and the ~130 ceiling is a
known non-RF open-host limit. So the plan is **re-scoped**: the RF campaign is demoted to a
contingency, and the live work is a clean confirmatory rebuild. **Design principle throughout:
pin and log interval + SDU + FSU-arm state (`spacing`) + per-event occupancy with every number;
freeze one archived build set; pair GATT/CoC back-to-back (ABBA).**

- **Test 0 — confirm the artifact + close the reconstructed-vs-original gap (STATUS: DONE
  2026-08-25).**
  - *Static (DONE):* audited `coc-f52.hex` — it is a **50 ms / 244 B / FSU-on** binary (interval
    hardcoded), so it could never have produced ~170; its own record is ~150/~131 at 50 ms. Sink
    source + credit Kconfig are **byte-identical** to the original (sha256 match), `RX_CREDITS=64`
    unchanged — the sink-credit-regression hypothesis is **not supported**.
  - *Dynamic (DONE):* rebuilt the matched **480 B / 15 ms / FSU-on** central (verified on air:
    `interval=12→15.00ms`, `spacing=52`, `PHY 2M`, `DLE 251`) and measured on the two nRF54L15-DKs.
    **10 cm → 161.9 KB/s, 10.4 PDU/event (reproduces the original ~170 operating point); ~2 ft →
    121.6 KB/s, 7.8 PDU/ev.** FSU-off control at 15 ms: 117.8 KB/s @ 2 ft (so FSU ~0% there, as
    expected when refill-starved). **Verdict: no build/config regression — the reconstructed
    central reproduces ~170 at close range; the 2 ft number is ordinary distance-sensitivity.**
    (Evidence: session captures not retained in this repo; `eagain=0, poolfail=0` throughout — refill-bound,
    not credit/RF-bound.)
- **Test A′ — paired GATT-vs-CoC, close-vs-far (STATUS: DONE 2026-08-25).** CoC-244 vs
  GATT-244-write, both 15 ms / no-FSU / 2M, at 10 cm and 2 ft. **Result: CoC 158→150 (5% drop),
  GATT 156→145 (7% drop) — indistinguishable.** CoC is NOT more RF-fragile than GATT. The larger
  drop earlier seen with 480 B CoC (~25%) is a **segmentation** effect (2-PDU SDUs, both must
  arrive), not a transport difference — see §4. Evidence:
  `debug-evidence/coc-vs-gatt-rangetest-20260825/`.
- **Test 1 — controlled degraded-RF campaign (STATUS: CONTINGENCY — only if Test A′ shows a real
  CoC-specific extra sensitivity worth a curve).** Paired GATT/CoC vs a monotonic attenuation axis
  (stepped distance or known-dB attenuator), counterbalanced, RSSI + on-air PER logged
  (`CONFIG_BT_CTLR_CONN_RSSI`; nRF Sniffer / Sniffle follower — a *following* sniffer; the pinned
  `fsu-onair-observer` can't track a hopping CoC link), n>=3, distributions kept.
- **Test 2 — credit-stall instrumentation (STATUS: CONTINGENCY, with Test 1).** Only if a real
  CoC-specific degradation ever appears whose mechanism needs identifying.
- **Test B′ — saturated-CoC bench: latency-under-load + the refill lever (STATUS: DONE
  2026-08-25, §11.2).** One bench that closes both §11 items:
  (i) **CoC stop-signal latency under CoC-bulk saturation** — mirror the GATT latency-under-load
  rig (serialized tiny stop-signal RTT + concurrent saturating bulk) but with the bulk on **CoC**,
  producing the missing **GATT-vs-CoC saturated/unsaturated comparison** and testing the
  backpressure→staleness claim (§8, §11.2) directly.
  (ii) ~~**Refill lever** — sweep the controller TX-buffer depth and watch PDU/event climb
  toward a ~35–41 airtime wall~~ **CLOSED 2026-08-26 (§11.1):** no refill lever exists — the
  per-event ceiling is tIFS-bounded airtime (buffer depth ≡ no effect; FSU +18.6%; host hands 1:1);
  the only levers are FSU / PHY / packing.
  Discipline: pin+log interval/SDU/`spacing`/distance, fresh reset per run (§gotchas).

Full protocol (build set, matched-config rebuild recipe, ABBA order, RSSI/PER capture, per-test
pass/fail) lives in `degraded-rf-campaign-plan.md`.

## 11. Two open items the numbers point to

### 11.1 The per-event ceiling is AIRTIME (tIFS-bounded), not a host-refill limit — RESOLVED 2026-08-26 (scope: 7.5–25 ms, 2M, 251 B PDU)
Earlier drafts framed the **~8–10 PDU/connection-event** at 15 ms as a *host→controller refill/
staging* limit sitting *below* a "~35–41 PDU airtime wall." A dedicated instrumentation campaign
(`debug-evidence/tx-staging-20260826/`) shows that framing was **wrong: the ~10 PDU/event IS the
airtime ceiling** — established **within the tested regime** (7.5–25 ms interval, 2M PHY, 251-octet
PDUs); do not extrapolate the absolute counts outside it.

**The anchor — the observed maxima match physical packet capacity exactly.** At 2M a 251-octet data
PDU is ~1048 µs on air; the peer's empty acknowledging PDU is ~44 µs; with tIFS = 150 µs a
`data → tIFS → empty-ACK → tIFS` cycle is ~1392 µs, so an event fits `floor(interval / 1392 µs)` =
**5 / 10 / 17 pairs at 7.5 / 15 / 25 ms** — exactly the measured maxima (`max = 5 / 10 / 17`; means
4.87 / 9.64 / 16.30). Shrink tIFS to 52 µs (FSU) and the cycle is ~1196 µs → **12 rather than 10 pairs
at 15 ms** (+20% by integer packing); the ~16% continuous per-pair saving quantizes up to that 10→12
step, matching the measured **FSU +18.6%** (9.66→11.50). **This exact capacity match is the
load-bearing evidence.**

Two further observations **converge** with it (each *consistent with* airtime but not, on its own,
*discriminating* it from a fixed-rate staging path):
- **Aired PDU/event scales linearly with interval** (4.87 / 9.64 / 16.30 at 7.5 / 15 / 25 ms;
  throughput flat ~155 KB/s). *Caveat:* a fixed time-rate host-staging path would also give
  PDU/ev ∝ interval — so the scaling alone doesn't distinguish the two; it is the *value* matching
  capacity (above) that does.
- **The host hands ~what airs, ~1:1** — a gated counter at `l2cap_data_pull`'s PDU-return
  (`l2cap_pull_pdus`, `CONFIG_BT_TESTING`) shows `host_pulls/ev ≈ aired/ev` (9.68 ≈ 9.69). *Caveat:*
  that hook fires when the lower path *requests* data, so it shows pulled packets air successfully; it
  does **not** independently prove the controller would have requested another packet had airtime
  remained.

Also **not buffer depth** — deep (64) ≡ shallow (8) ACL-TX at 15 ms (9.64 vs 9.70 PDU/ev, *this
bundle*). `eagain=0` / `poolfail=0` mean the app pool and app-visible credits never blocked, but do
**not** by themselves exclude an L2CAP/HCI credit ceiling — the capacity match is what rules that out.
The old "~35–41 wall" was a **~50 ms** figure (0.65 PDU/ms × 50 ≈ 32) compared against a 15 ms
occupancy — an interval mismatch.

**Consequence (within this regime):** no separate "open-stack refill lever" — the per-event ceiling
is airtime, moved only by **FSU / PHY / packing**, not by any host TX-staging rework. The once-planned
`lll_conn.c` / `tx_processor` "per-event staging" instrumentation is **closed as not-a-bug**. A paired
peripheral-uplink DLE=251 stall probed in the same campaign flowed normally (host stages 1:1) and
**did not reproduce across the four channel-opens tested** — i.e. no *persistent* throttle in those
opens (not a proof that none can occur); it is intermittent and unrelated to this ceiling. *(Zephyr doc backing — LE-host NoCP / `BT_BUF_ACL_TX_COUNT`, the L2CAP/controller
Kconfig, per-knob links — curated in [`zephyr-doc-references.md`](../references/zephyr-doc-references.md).)*

### 11.2 CoC latency-under-load — MEASURED (2026-08-25), and it confirms the recommendation
The backpressure→staleness argument was previously GATT-only and design-reasoned for CoC. It is
now measured with an all-CoC rig (`coclat-central`/`coclat-sink`: a serialized tiny stop-signal
ping-pong rides the *same* CoC channel as the bulk), 2× nRF54L15 @10 cm / **pinned 15 ms**
(sink `PREF=12` + `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` — see the interval-drift gotcha). Evidence:
`debug-evidence/coc-latency-under-load-20260825/`.

**Idle floor (unsaturated):** **24.8 ms**, flat, 0% over budget — *identical* FSU-on vs FSU-off
(FSU is neutral at idle) and ≈ GATT idle. **CoC and GATT are equal at idle; the whole gap is under
load.**

**Under saturation:** the stop-signal cliffs to **~290 ms (100% >30 ms)** — ~8× GATT's ~35 ms. It
has no app-layer priority, so it queues FIFO behind the full bulk pool + credit window, and the
queue drains at only ~10 PDU/event (§11.1 airtime limit — *the latency divisor*), doubled by 480 B
segmentation (2 PDU/SDU). Completion-pacing (shallow pool) helps but is **not enough**: pool 64→4
cut it 290→**~107 ms** with no throughput loss (~162 KB/s), still 100% over budget, ~3× GATT.

**The offered-load curve (fills the unsaturated cell) — a sharp knee AT the ceiling:**

| offered | delivered | stop-signal RTT | >30 ms |
|---|---|---|---|
| 40 KB/s | 38 (1:1) | 13.3 ms | 0% |
| 80 KB/s | 76 (1:1) | 9.9 ms | 0% |
| 120 KB/s | 113 (1:1) | 9.7 ms | 0% |
| 160 KB/s | 153 (ceiling) | 273 ms | 100% |
| saturate | 155 (ceiling) | 290 ms | 100% |

Below ~78% of the ~155 KB/s ceiling, delivered tracks offered 1:1 **and** the stop-signal is
~10 ms (0% over budget); at saturation it explodes 20×. **The hazard is *saturation*, not the
transport.** Design rule: never saturate the safety link — keep bulk below ~75% of ceiling. In this CoC
offered-load rig FSU was neutral on latency at idle and below the knee; on GATT with the bursty load
ramp it also lowers the tail below the ceiling (2026-10-03/04 note below).

**Matched GATT offered-load curve (from `latency-under-load-20260813/hardening-20260814`):**

| offered | GATT-naive >30 ms | **GATT completion-paced** (p99 / >30 ms) | CoC-unpaced (RTT / >30 ms) |
|---|---|---|---|
| idle | 0.1% | 19 ms / 0% | 24.8 ms / 0% |
| 40–75 | 7–30% | 26 ms / <0.2% | ~10 ms / 0% |
| 100–120 | 47% | 26 ms / <0.6% | ~10 ms / 0% |
| **saturate** | 100% (p99 60 ms) | **28 ms / <0.6%** | **~290 ms / 100%** |

**The decisive safety-link finding: GATT + completion-pacing NEVER cliffs** — p99 ≤28 ms, <0.6%
over budget, *at every offered load including saturation* (a flat plateau; ≤1 outstanding write, so
the stop-signal is never behind more than one). CoC below the ceiling is excellent (~10 ms, even
beats GATT-paced) but it is a **knife-edge**: it cliffs to ~290 ms the instant you saturate, and
~107 ms *even paced* (segmentation + refill floor). GATT-naive degrades gradually but caps ~60 ms.
**So GATT+pacing wins on saturation-ROBUSTNESS, not best-case latency:** it holds ~28 ms whether
at 10% or 100% load, whereas CoC has zero margin — one saturation event and the stop-signal is
290 ms stale. For a safety link, robustness-to-saturation beats best-case latency.

**FSU-armed CoC offered-load (spacing=52 verified, 2026-08-25):** FSU is **neutral on latency
below the knee** (~10 ms FSU-on == FSU-off) and **raises the saturation ceiling +16% (155 → 180
KB/s)**. It thereby **widens the safe zone** — at 160 KB/s, FSU-off is *at* the ceiling → cliffs to
273 ms, but FSU-on is *below* the new 180 ceiling → back to 9.8 ms / 0%. FSU moves the knee right
(more bulk headroom before the cliff) but past the higher ceiling it still cliffs (~249 ms): **FSU
buys operating margin, not latency immunity.** (App gotcha found here: requesting FSU before
PHY=2M + full DLE returns `-EACCES`; gate the request on DLE `tx_max≥251` — REPRODUCE.md.)

*(Update 2026-10-03/04, GATT stop-signal latency with FSU, matched arms: FSU lowers the loaded p99 by
~2–9 ms at 7.5 ms (unpaced and paced) and 25 ms, **including below the ceiling**, with idle latency
unchanged and no timeouts. Mechanism, measured with the open controller's per-event counter and
predicted in advance: a full 7.5 ms event holds 5 bulk exchanges without FSU and 6 with it, and the
z54-lat load ramp sends each second's quota as a burst, so events fill even at 25 KB/s and the queue
drains ~20% faster. Nordic SDC shows the same effect. The CoC "neutral below the knee" result above
may reflect that rig's load pattern (not checked). [`latency-fsu-20261003`](../../debug-evidence/latency-fsu-20261003/README.md),
[`latency-fsu-20261004`](../../debug-evidence/latency-fsu-20261004/README.md),
[`replication-20261004`](../../debug-evidence/replication-20261004/README.md).)*

### 11.3 The separate-channel priority-lane test — 2026-08-25, re-measured 2026-09-09: a real ~9× win, but just over a 30 ms budget

> **Re-measured result (supersedes the single-session numbers below):** over the sustained saturation
> window (t ≥ 10 s, n=8, reset-isolated, FSU held), the separate-channel control RTT was **32.2 ms ±
> 0.3 (Student-t), 100% of samples over 30 ms, worst 87.1 ms**, with the control channel never starved
> (0/8, 0 timeouts) on v4.4.2-16
> ([`coc-dedicated-lane-retest-20260909`](../../debug-evidence/coc-dedicated-lane-retest-20260909/README.md)).
> That is a reliable ~9× improvement over the shared channel (~290 ms), but it does **not** meet a 30 ms
> responsiveness budget. The original "~33 ms" below quoted the first saturation window of one run.
**Result (prediction was wrong, favorably): a separate CoC control channel eliminates the cliff.**
Two-channel rig (`coclat2-*`: bulk 480 B blast on PSM 0x0080 + own tx_pool; stop-signal ping-pong
on a *separate* PSM 0x0081 + own tx_pool/credits), pinned 15 ms, FSU armed (`spacing=52`):

| control path, under bulk saturation | RTT | vs |
|---|---|---|
| **separate CoC channel** (this test) | **~33 ms** (min 19, max 69), bulk 179 KB/s unpaced | — |
| shared CoC channel (§11.2) | ~290 ms / 100% >30 ms | 9× worse |
| GATT completion-paced | ~28 ms / <0.6% | comparable |
| CoC idle floor | 24.8 ms | +8 ms |

So Zephyr's L2CAP/LL **does** interleave channels — the tiny control SDU rides its own credit
window and is scheduled promptly instead of queuing behind the bulk FIFO. The predicted LL-merge
floor did **not** materialize; the control stays ~idle-level (~33 ms) while bulk blasts *unpaced*
at 179 KB/s. **In this one session, a separate CoC channel behaved like a priority lane** — Zephyr's
L2CAP/LL interleaved the channels (the control SDU rides its own credit window) rather than FIFO-
queuing the control behind the bulk.

**What this suggests (single session — not yet validated):**
- **A dedicated CoC control channel looks promising as a control lane** — ~33 ms while bulk blasted
  unpaced at 179 KB/s, with **no app-layer bulk discipline**. That is the property GATT+pacing
  *cannot* offer (pacing needs the app to keep ≤1 write outstanding, infeasible for bursty/uncontrolled
  bulk). **But this is n=1** (max 69 ms, over a strict 30 ms budget), and it is **in tension with
  §11.4/§11.1**: a separate L2CAP channel still shares one ACL connection's per-event airtime. The
  now-**valid** EATT test (§11.4 — also a separate-bearer scheme) only reached a **~106 ms median at
  saturation**, far worse than this ~33 ms — so if the two are mechanistically equivalent, this ~33 ms
  is suspiciously good for n=1. **The mechanism is UNRESOLVED** and needs a **matched CoC-channel-vs-EATT,
  percentile/soak experiment before "validated."**
- **GATT does NOT get the same lane (MEASURED, 2026-08-25).** A matched test — GATT stop-signal on a
  *separate characteristic*, deep ACL queue, unpaced bulk — degraded to **~71 ms mean / ~200 ms max**
  (vs the separate CoC channel's ~33 ms). Classic GATT has no per-characteristic prioritization (one
  ATT bearer / one queue). GATT would reach a lane only via **pacing** or **EATT** (measured in §11.4: a tail-distribution tradeoff, not a 30 ms fix).
- **Recommendation nuance:** GATT+pacing is the simplest *measured* mitigation for *rate-controllable*
  bulk; for *uncontrolled* bulk a **dedicated CoC control channel** is the most promising option seen,
  but on n=1 — call it a *candidate* architecture, not a validated one.

Bound: single session @10 cm; ~33 ms already exceeds a strict 30 ms **application stop-deadline**
(idle floor 24.8 ms + ~1 event of airtime sharing), and the **max of 69 ms exceeds even a 50 ms
deadline** — so a percentile/soak/n-reps run is required before this is a safety claim. (Note: this
is an app-level latency deadline, *not* the BLE link-supervision timeout.)

### 11.4 EATT (GATT-over-multiple-bearers) — MEASURED, publication-grade (preregistered gate PASS, 2026-08-27): a tail-distribution tradeoff, not a demonstrated 30 ms safety-budget mitigation
§11.3 predicted GATT could reach a control lane via **EATT** (multiple L2CAP ECRED bearers). Measured
with a **preregistered-gate ABBA run** (`debug-evidence/eatt-latency-20260826/preregistered-20260827/`;
the 2026-08-26 first run was invalid — a seq-correlation defect; a matching-pong rerun then confirmed
it *by source semantics via a documented gate correction*; this run declares the corrected gate up
front and **passes it**). Corrected gate (all 4 arms): no `late_pong` before the first timeout;
cumulative `late_pong ≤ timeouts`; recorded RTTs are exact-seq matches. **Timeouts counted as budget
failures; p99 omitted (the 200 ms timeout censors it); completed-sample median reported.**
- **Severe tail (`>100 ms-or-timeout`) — replicates at saturation** (per-replicate ON 65.9/66.7 % vs
  OFF 73.0/82.0 %), but the benefit is **negligible at low–mid load** (within ~1 point at 50–100 KB/s;
  at 50 KB/s EATT is even *slightly worse*, 6.6 vs 6.4 %). Do **not** say "lower at every load."
- **Completed-sample median — replicates directionally** below OFF's ~193 ms mode: **replicate medians
  ON 141/152 ms vs OFF 193/193 ms** (the "~146 ms" pooled figure is a **median of run medians**, not a
  pooled sample median).
- **30 ms-violation & timeout rates — NOT directionally stable at saturation.** EATT generally raised
  `>30 ms-or-timeout` **through 125 KB/s**, but at 150 KB/s the OFF arm varies enough that the direction
  reverses (OFF 80.3/90.6 %; timeout 5.1/16.9 %) — so "worsens at every load" is too strong at saturation
  (and the 25 KB/s timeout is ≈0 for both).
- **Neither is remotely acceptable for a 30 ms deadline at saturation** (both fail >85 % pooled). With
  EATT the distribution is **consistent with reduced bearer-level head-of-line blocking**, but bearer
  assignment is unmeasured (mechanism **inferred, not shown**), and it still shares one connection's
  per-event airtime (§11.1). **Net: EATT is a tail-distribution tradeoff, not a demonstrated 30 ms
  safety-budget mitigation.**

**CoC-channel-vs-EATT tension — open (do not read as equivalence).** Raw CoC's separate channel held
~33 ms under near-saturated bulk in its first run (§11.3; re-measured at **32.2 ms, n=8, 100% over 30 ms**), far better than EATT's ~146 ms saturated median. That
single point is not matched closely enough to establish either equivalence or CoC superiority; a matched,
percentile/soak CoC-channel-vs-EATT experiment is needed. For now: **completion-pacing is the only
mitigation *measured* to hold a 30 ms budget under saturation (§11.2).** (Two real config
pitfalls also surfaced — a silent Kconfig dependency drop and the EATT bearer MTU = `BUF_ACL_RX_SIZE − 6`;
both in REPRODUCE.)

---
*Historical (superseded by the result above): the plan + prediction.*

### 11.3-plan (superseded) The separate-channel priority-lane test
**The question it answers: can BLE give the stop-signal a *stack-level* protected lane on a shared
connection, or must safety rely on *app-layer discipline*?** Everything that fixes the cliff so far
(don't-saturate; GATT+completion-pacing) needs the **application to behave**. A separate CoC channel
for the control message would be a **link-guaranteed** lane — the stop-signal protected *even if the
bulk misbehaves*. For a safety link that distinction is decisive, and it matters exactly in the
**uncontrolled/bursty-bulk** regime (a robot flushing a video buffer) where "don't saturate" is not
achievable.

**Mechanism prediction (from the §11.2 decomposition).** The ~290 ms is two stacked queues: the
app/L2CAP queue (shared tx_pool + credit window — pool 64→4 removed ~183 ms of it, leaving ~107 ms)
and the **LL TX queue** (the controller's single ACL queue where all channels merge and drain at
~10 PDU/event, §11.1). A separate L2CAP channel gives the control its own pool + own credits →
eliminates the app-queue wait, **but both channels still merge at the one LL TX queue**. So the
predicted result is the **LL floor (~50–107 ms)** — better than 290 ms but *still over the 30 ms
budget*, because the control PDU still queues behind bulk PDUs at the link layer.

**Decision it drives:**
- **If it lands at the LL floor (still >budget)** → a separate *channel* is not enough; only a
  separate *connection* (independent connection events, ~2× overhead) or don't-saturate/GATT-paced
  gives real protection. Kills "dedicated CoC control channel" as an adequate safety architecture.
- **If it stays ~10 ms under bulk saturation** (Zephyr L2CAP/LL interleaves so the tiny SDU jumps
  ahead) → BLE CoC would have a stack-level priority-lane behavior, making a "dedicated CoC control
  channel" a candidate control lane vs GATT+pacing (LL interleaving vs app-discipline). *(The test ran
  and pointed this way on n=1; the n=8 re-measure gave 32.2 ms with 100% of samples over 30 ms — see
  §11.3. Mechanism UNRESOLVED; not "validated"/"guaranteed.")*

Either result is decision-changing. Bonus: first empirical test of US 6,922,548's assertion
(separate L2CAP channel limits control latency) on BLE LE CoC (§ prior-art), and the result
transfers to GATT (separate characteristic has the same LL-merge question).

**Plan.** Two-channel rig (`coclat2-central`/`coclat2-sink`): bulk (480 B blast) on CoC PSM
0x0080 with its own tx_pool, control (ping-pong stop-signal) on a *separate* CoC PSM 0x0081 with
its own tx_pool + credit window. Saturate the bulk channel; measure the control-channel RTT.
Compare to the shared-channel ~290 ms and the GATT-paced ~28 ms. Pinned 15 ms, fresh reset, verify
both channels up. Only worth the two apps if the deployment can't guarantee non-saturation — which
the shipyard/bursty-video case cannot, hence running it.

## 12. Open items after the 2026-10-06 credit-bug corrections
1. ~~Zephyr CoC duplex at 25 ms, FSU off, 154.5 KB/s~~ **Resolved:** credit-loop starvation from the 64-deep controller
   queue; with 20-deep queues Zephyr = SDC in every cell ([`coc-duplex-25ms-diag-20261006`](../../debug-evidence/coc-duplex-25ms-diag-20261006/README.md),
   [`coc-duplex-fsu-q20-20261006`](../../debug-evidence/coc-duplex-fsu-q20-20261006/README.md)). Why it bites only at 25 ms FSU off is not established.
   SDC's one-way deficit at short intervals is accounted for by a model fit (SDC budgets for a maximum-length reply;
   [`oneway-exchange-model-20261006`](../../debug-evidence/oneway-exchange-model-20261006/README.md)), inferred not observed.
2. ~~Replicate the corrected CoC results on a second day~~ **Done 2026-10-07:** every cell kept its verdict (byte-identical
   firmware, same placement; [`replication-20261007`](../../debug-evidence/replication-20261007/README.md)). A placement-varied repeat remains optional.
3. **Older CoC rigs (audited 2026-10-06, read-only):** unaffected: CoC latency under load (§11.2/§11.3), the dedicated-lane
   retest, reset-recovery-100 (the leak fired but recovery and timing don't depend on credits; data flowed on all 100;
   it cannot have exercised the 0-credit stall, since its sink only transmits after a ping), nRF52-era throughput (7.5 ms,
   FSU off). Affected: `latency-envelope-20260814`'s throughput column and knee (per-segment sink). Possibly affected:
   `rf-range-safety-20260909` loaded-ping RTT (sink not re-flashed, leak fired). The older wedge write-ups
   (`uplink-coc-diag-20260812`, `apps/nrf52/nrf52-l2cap-echo/zephyr-bug-report.md`) describe the 0-initial-credit stall.
   Fixed 2026-10-07: `coclat-central`, `coclat2-central` (window in the request, stale credits dropped), `coc-sink` (stale
   credits dropped; per-segment returns stay the default, batching opt-in). Also fixed 2026-10-07: `z54-uplink-central`,
   `z44-uplink-central` (window in the request; stale credits dropped),
   `z54-sink` / `z44-dk-sink` (stale credits dropped; per-segment returns kept so the nRF52-era
   results rebuild, with a comment on their FSU cost). `z44-repro-initiator` is deliberately left with 0 initial credits:
   it reproduces the Zephyr stall (`zephyr-l2cap-zero-credit-repro-20261007`; fix tested in `zephyr-l2cap-fix-test-20261007`).
4. ~~Guards the tools lack~~ **Done (2026-10-06):** `tools/link_gates.py` gates PHY, data length, post-connect interval
   changes and the CoC credit window in all four campaign tools.
5. **Upstream: filed** as [zephyrproject-rtos/zephyr#121544](https://github.com/zephyrproject-rtos/zephyr/issues/121544)
   (2026-10-07, by teleop-bench) with the minimal repro and the tested host fix (`zephyr-l2cap-zero-credit-repro-20261007`,
   `zephyr-l2cap-fix-test-20261007`). Follow-up: link the repo in a comment once BLE-bench is public.
6. **Re-verify CoC on Zephyr 4.5 when it is released** (v4.4.2 is the latest release as of 2026-10-07; v4.5.0-rc1 is out;
   per Zephyr's release plan, RC2 2026-10-12, hard freeze/RC3 2026-10-19, release the week of 2026-10-26, so a re-check from
   the hard freeze on is representative).
   4.5 moves L2CAP channel RX work from the system workqueue to the Bluetooth workqueue (`22896cb8d6f2`, which shifts
   credit-return timing) and converts the TX path to a host mutex (`210f14bc4501`). Port the fsu-m0 series (a few
   mechanical conflicts in `pdu.h` / `lll_conn.c`), drop the removed Kconfig options, then re-run one-way CoC at 7.5 and
   25 ms (both credit policies), CoC duplex at 25 ms, and one GATT cell as a control (~1–1.5 h). The 4.4 maintenance
   branch (210 commits after v4.4.2) contains nothing expected to change a number; the 0-initial-credit bug is present in
   both 4.4.2 and 4.5.0-rc1.

## Provenance — and its gaps

**Relation to upstream Zephyr — by the ACTUAL resolved build, not by which feature was measured.**
The tier is the binary you ran, not whether FSU was the variable:
- **Stock baseline** — *unmodified, released Zephyr v4.4.1* (base commit `1f6485e`), plain `west build`.
  This applies only to builds with **no `fsu-m0` patches at all**.
- **Patched tree (`fsu-m0`)** — *v4.4.1 + the 16-patch series* (`git am`, reproduces the tree
  byte-identical; verified). **This tier includes the TX-staging and EATT cells** — they ran on
  `fsu-m0` (they use gated instrumentation that is *part of the series*: the event-fill DIAG and the
  `l2cap_pull_pdus` counter are patches 0015/0016). The instrumentation **compiles out** of a real
  build, but the binaries these cells actually ran are **not demonstrated byte-equivalent to stock**,
  so treat them as patched-tree unless/until that equivalence is shown. The series is one *feature* —
  **Bluetooth 6.0 Frame Space Update**, imported from the still-open upstream PRs (#82324 → #99473),
  defect-fixed to a working, on-air-verified state (tIFS 150→52 µs), **ahead of mainline open Zephyr**
  — plus the gated measurement scaffolding.
- **Proprietary (SDC)** — Nordic SoftDevice Controller (NCS v3.4.0), closed; ships FSU (65–70 µs floor
  vs the open stack's 52 µs).

Committed: the apps, the 16-patch series, prebuilt hexes + sha256, build/run steps (`REPRODUCE.md`,
`zephyr-patches/fsu-m0-series/`, `debug-evidence/`, `prebuilt-hexes/`, `zephyr-doc-references.md`),
the prior-art log. **Archival gaps to close before calling a cell fully reproducible:** the
TX-staging archive holds HEX only (the resolved `.config` for the key builds — `s1-A2`, `s1-fon`,
`s2-cen` — is captured in `debug-evidence/tx-staging-20260826/configs/`; ELF / `build_info.yml` /
per-image source revision are **not** archived), and the EATT archive holds logs + run script +
resolved `.config` (its *result* is quarantined regardless — §11.4). So "exact recipes" reproduces
the *deltas*, not a byte-for-byte binary.

**Not fully provenanced — read before citing §4/§9:**
- The **~170 baseline and the ~131 re-flash are different builds/intervals** — `coc-f52.hex` is a
  50 ms-hardcoded binary that measured ~150/~131 at 50 ms, never ~170; the ~170 was a 480 B /
  12.5–15 ms rebuild (§4/§9.5). Any "170→131" comparison is confounded by operating point.
- The **original 08-13 sink hex is not archived** (only its `main.c`/config, which are
  byte-identical to the rebuilt sink), so the campaign was **not** same-firmware end-to-end —
  though the sink credit path is provably unchanged (sha256 match).
- The **decisive re-flash campaign's raw serial logs, live `.config`, and run-order** live in
  a temporary working directory and were **not retained** in this repo; the committed `full-matrix-rerun-20260824/`
  holds the CSV + FINDINGS + scripts, not those raw captures, and its `FINDINGS.md` **still
  asserts the retracted RF claim** — §4 supersedes it. The sweep was **single-run**.
- **No RF telemetry** (RSSI/PER/retransmit/spectrum) was captured in the degraded (distance) runs,
  so they are **2-point drops, not degradation curves**. *(No longer about distinguishing airtime-
  vs host-refill-bound — §11.1 settled the ceiling as tIFS airtime by a separate method
  [`tx-staging-investigation.md`]. The residual value is forward-looking: a stepped-attenuation sweep
  with RSSI/PER would turn the distance drops into curves — the recommended next benchmark.)*

Absolute KB/s are per-session/operating-point. The RF-robustness question is **not open pending a
campaign** — on current evidence there is **no indication of CoC RF-fragility** to resolve; a
campaign is warranted only if §10 Test 0's matched-config rebuild leaves a real gap.
