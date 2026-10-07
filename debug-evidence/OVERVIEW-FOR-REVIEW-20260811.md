# BLE 6.0 FSU on-air verification — status overview for external review
*2026-08-11. Honest status; distinguishes PROVEN from PENDING. Nothing here is
spec-quoted directly — see `zephyr-patches/fsu-m0-cheatsheet.md` for source tiers.*

## 1. Objective & why it matters

The M0 work claims a **+5.72 % throughput** gain from BLE 6.0 **Frame Space Update
(FSU)** — negotiating the inter-frame spacing (tIFS) below the 150 µs default. That
claim currently rests on **indirect** evidence (throughput CI; an on-chip −102..−103
µs round-trip "cgap" step vs a registered −100 ± 15). The M0 cheatsheet states
verbatim: *"Qualified on-air capture remains the formal physical gate … Direct
on-air verification remains the only pending Phase-4 item."*

This effort builds and **qualifies a passive raw-radio observer** (a third device
that measures the achieved tIFS **on air**, independent of the endpoints) to supply
that missing direct physical confirmation. No public tool measuring achieved FSU
on-air spacing was found (dated search 2026-08-10; Wireshark tracks the FSU opcodes
but not the µs fields).

## 2. Method — the qualification ladder

The observer is only trustworthy if its own measurement chain is proven first, so
it is qualified through a ladder, each rung gated by a **frozen acceptance protocol
written before data** and a self-tested analyzer. "Necessary-not-sufficient": each
rung must pass before the next is meaningful.

| rung | what it proves | status |
|---|---|---|
| **Q0** | pipeline integrity — timestamps grounded to ±1 tick (62.5 ns) | **PASS** |
| **Q1** | close-pair retention + gap discrimination vs a synthetic generator (52/70 µs, 1M+2M, both DKs), ±1 tick | **PASS** |
| **Q2** | a live connection using a pinned two-channel map, two endpoints, near/far in BOTH role assignments; measures the live intra-event tIFS and attributes it | **COMPLETE** |
| **Q3a** | the FSU verdict — reduced tIFS on air, cross-validated (on-air primary) | **SCOPED + IMPLEMENTATION STARTED; not yet run** |

## 3. Q2 result — the qualified instrument (the main recent milestone)

On a live nRF54L15 ↔ nRF54L15 connection (open `ll_sw_split` controller, 1 M, 7.5 ms
interval, channel map pinned to {10,11}, observer nRF52832 parked on ch10, 30 s
captures), all committed. Provenance disclosure: the SECOND cell of each accepted
pair (symctl2, nearfarA2, nearfarB2) records `tree_dirty=true` because the first
cell's evidence was still uncommitted when the second ran; the firmware/tool/config
hashes remain matched (the evidence is intact), but "clean provenance" warrants this
note.

- **Frozen calibration:** two reset-isolated symmetric controls →
  `gap_proxy_dev = 3047 ticks` (cross-cell spread 0), i.e. **calibrated
  tIFS = (3047 − 639)/16 = 150.5 µs — within 0.5 µs of the nominal 150 µs value**
  (the +8-tick offset vs the 3039-tick nominal is unexplained and is characterised
  in Q3a, not assumed away). Retention 98.3 % / 99.1 %.
- **Near/far config A** (central near): 2 cells, far-side retention 97.9 % / 98.4 %,
  separation 40–42 dB, far-side −77/−78 dBm (in the −85..−70 weak band), 0
  attribution disagreements, gap_proxy within ±2 ticks of frozen.
- **Near/far config B** (peripheral near, roles swapped): 2 cells, far-side (now
  central) retention 95.6 % / 96.0 %, separation 41–42 dB, far −72 dBm, 0
  disagreements, gap_proxy **|d| = 0** vs frozen.
- **Role-swap check:** PASS — the strong population flips from first-in-event
  (config A) to second-in-event (config B); both configs accept under the frozen
  reference.

**Interpretation:** the observer measures the live tIFS to instrument resolution,
attributes it correctly regardless of which endpoint is near/far, and reproduces
the ~150 µs value across a 40+ dB near/far span — showing the observer estimate was
invariant across the tested near/far geometry. The instrument is qualified to
measure a *reduced* tIFS. (The explicit role-swap command + output is archived at
`debug-evidence/observer-q2-roleswap-20260811.txt`, not only in the commit message.)

`gap_proxy` = observer's prev-packet-END → next-packet-ADDRESS = tIFS·16 + 640 − 1.
The 639-tick offset is preamble+AA at 1 M (a physical constant, **tIFS-independent**),
so the calibrated-tIFS formula transfers unchanged to the reduced-tIFS regime.

## 4. Measurement-chain rigor (for reviewer confidence)

The acceptance machinery was hardened across ~9 external review cycles
(protocol rev-1 → rev-12). It is designed so a **false-accept is structurally hard**:

- **Non-circular retention denominators** from controller-owned free-running
  counters, bracketed by an atomic START/GO/END snapshot handshake on a single host
  clock (denominator ≥ true count → retention reported as a **lower bound**).
- **Combined snapshot slop gated ≤ 3 %** of the capture window (why the window was
  lengthened 10 s → 30 s: dilutes fixed slop to <1 % so the retention lower bound is
  robust, not slop-dragged).
- **Provenance-bound calibration artifact:** the frozen reference is produced only
  by `combine_calib.py` from ≥2 accepted controls (median-of-medians,
  round-half-to-even, every control within ±2 ticks), with per-input log +
  manifest sha256, endpoint firmware/config hashes, and tool/protocol lineage.
- **`validate_calib()` (the consumer) re-verifies the WHOLE contract** at use time:
  contract fields, lineage vs current tools, AND it **re-resolves each input dir,
  rehashes the evidence, RE-ANALYZES the raw logs to reproduce each median, and
  reapplies the homogeneity gate** — a syntactically-valid but fabricated or
  edited artifact is rejected.
- **Endpoint provenance is verified, not assumed:** the runner **flashes** the
  supplied build dirs (hashed image == running image) and requires real 64-hex
  digests; a nonexistent/`MISSING` build → QUARANTINED, never ACCEPT.
- **`--smoke` mode** forces a non-accept verdict so an integration smoke can never
  become the first accepted cell.
- **~81 analyzer/combiner/runner self-tests** (invisible-event denominators,
  pairing, attribution, role-swap, RSSI gates, slop, forgery/lineage/evidence
  -rehash, verdict/quarantine table, homogeneity, digest schema) — all green.

Q2 evidence is committed on the **`ble5-l2cap-throughput-benchmark`** branch of
this (zenoh-pico-ble-test) repo (see §7).

## 5. Q3a — the FSU verdict (scoped, drafted, implementation started)

Scope: **Q3a only** — one arm, **150 → 100 µs at 1 M** (the registered observable;
the 52 µs floor / 2 M / generality are deferred). Protocol:
`debug-evidence/observer-q3-20260811/Q3-ACCEPTANCE-PROTOCOL.md` (rev-1 DRAFT).

**Primary metric — the offset-cancelling on-air STEP.** The Q2 baseline carries an
UNEXPLAINED +8-tick offset (3047 vs the 3039-tick nominal for 150 µs). IF that
offset is tIFS-independent it cancels in a difference, so the negotiated 50 µs
reduction should give **step = median(f150) − median(f100) = 800 ticks** (and the
f100 plateau would be 2239 + 8 = **2247**, NOT 2239 — one cannot gate both a
2239 plateau and an 800 step, since 3047 − 2239 = 808). Therefore:
- **PRIMARY (gate):** `|step − 800| ≤ 4 ticks` (= 50 ± 0.25 µs). The ±4 is
  preregistered from Q2's demonstrated ±2-tick per-plateau cross-cell
  reproducibility (two plateaus → their difference carries the sum) — NOT the
  ±2-tick instrument resolution.
- **SECONDARY / diagnostic:** the absolute plateaus (f150 ≈ 3047, f100 measured)
  are RECORDED to characterise the +8-tick offset's origin/persistence, not gated,
  until that offset is understood.

**Direction — Q3a directly observes ONE turnaround.** The observer's gap is the
central-END → peripheral-ADDRESS interval, i.e. the peripheral's response
turnaround; the reverse (central) turnaround is separated by the connection
interval and is NOT observed in a single-PDU event. The core verdict is scoped to
this observed direction; the earlier "per direction / ×2 = −100 µs round trip"
framing is RETRACTED. A reverse-direction companion (multi-PDU events so the
observer sees alternating turnarounds) is the one addition worth making — higher
priority than a second spacing — and is planned, not yet implemented.

**Cross-validation is step-wise and NOT symmetric across spacings.** f150 has no
HCI completion (no-request control) → on-air + on-chip **baseline** only. f100 has
all three: HCI control-plane (evt 0x35 SELECTED = 100, on the central) + on-chip
proxy + on-air. Peer participation is NOT proven by the initiator field alone — it
requires a **peripheral-side FSU completion at the same AA/session** (a callback
still to be ADDED to q2-periph). The on-chip leg is gated by
`CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH` (rev-0 wrongly named `INSTRUMENTATION`) and must
be captured/drained **on the PERIPHERAL** (the same turnaround the observer sees; a
central-only drain measures a different interval); that symbol + drain are NOT yet
wired, so the smoke starts on-air + HCI. Two outcomes are kept distinct: (a)
reduction physically observed vs (b) exact 800 ± 4-tick agreement — a ~790-tick
step fails (b) but is NOT "no reduction." The ±4 primary gate is IMMUTABLE (the
smoke may refine only integration/secondary diagnostics).

**Derisking status (fail-fast before freezing):**
1. Widened the analyzer pair-gap window 2900–3200 → 1200–3300 ticks — a latent bug
   that would have REJECTED every ~2247-tick 100 µs pair and faked a null. *(done)*
2. Added the central FSU trigger (`bt_conn_le_frame_space_update`, gated by
   `APP_FSU_MIN/MAX_US`; central `frame_space_updated` cb). *(done, but auto-fires
   1 s after connect — a runtime `F` trigger + Q3 runner timing is REQUIRED for the
   mid-step primary; the Q3 STEP analyzer, transition exclusion, and 800-tick
   estimator + their self-tests are NOT yet implemented — the committed pair-window
   change only proves Q2 didn't regress.)*
3. FSU build config: host `BT_LE_EXTENDED_FEAT_SET`, controller `EXTENDED_FEAT_SET`,
   and `CONN_INTERVAL_LOW_LATENCY` (for the 52 floor). *(in progress)*
4. **Because the pair-window change alters `analyze_q2.py`'s hash**, `validate_calib`
   correctly REJECTS the Q2 artifact (verified). Plan: commit the Q3 analyzer, then
   RECOMBINE the original Q2 controls into a NEW Q3-named calibration artifact,
   PRESERVING the Q2 artifact. *(pending)*
5. **Next: a quarantined go/no-go smoke** (on-air + HCI): does the plateau step
   3047 → ~2247, stable 30 s, unimodal? measure the +8 offset; confirm the
   peripheral-side completion. **Then** freeze rev-2 and collect ABBA-counterbalanced
   + (preferably) mid-connection-step cells.

The effect (~800 ticks) vs the per-capture IQR (8–10) is an **effect-to-IQR ratio
≈ 80** — it makes the step's *existence* unambiguous, not its accuracy 1/80 tick.
The residual risk is confounds, addressed by the f150 control and the
within-connection (mid-step) design.

## 6. Honest limitations / deviations / open risks

- **The core question is still OPEN:** whether FSU changes the *on-air* spacing is
  exactly what Q3a exists to test. If the smoke shows no step, that is a major
  finding contradicting the indirect evidence — and we stop and diagnose, not
  paper over it.
- **Bench feature shim:** feature bit 65 is forced via `BT_CTLR_FSU_BENCH_FORCE_FEAT`
  (documented bench-only, NOT a spec-correct extended-feature exchange) — acceptable
  for a scoped bench PHYSICAL claim, not a conformance claim, provided peer
  participation is independently logged. Guard: the HCI 0x35 `initiator` field alone
  is INSUFFICIENT; peer participation requires a peripheral-side FSU completion at
  the same AA/session (to be added). Catching the on-air 0x3B/0x3C PDUs is a useful
  but OPTIONAL extra (the parked observer may miss a ch11 control event).
- **M0 controller simplifications** (from the cheatsheet): immediate-adoption both
  ends, reductions-only, transitional-receive-windows added late (a link-death bug
  fixed en route). Direct spec-text verification is outstanding.
- **Observer generality is intentionally limited** (deferred to Q3b/c/d): 1 M only,
  channel map pinned {10,11}, AA read from a patched controller diagnostic (not
  auto-discovered from CONNECT_IND), no full 37-channel hop-following. These are not
  needed for the controlled-bench FSU verdict but bound the claim's generality.
- **On-chip M0 6.1 instrument** has had 4 revisions and a retracted flagship arm →
  treated as the WEAKER cross-val leg; the independently-qualified observer is
  primary. Must be drained on the PERIPHERAL to match the observed turnaround.
- **Controller source not yet pinned:** the Zephyr `fsu-m0` checkout has FIVE
  modified controller files (the Q2 diagnostic patch, uncommitted). Accepted Q2
  firmware is pinned by build-hash, but before accepted Q3 data these must be
  committed/exported/pinned for reproducibility.
- **Reverse-direction is a separate Q3b draft**, not part of the Q3a verdict; it
  needs its own frozen preregistration (traffic, per-turnaround attribution,
  sample counts, acceptance) before any collection.
- **Dev-kit RF reality:** the USB cable is the dominant 2.4 GHz radiator, so RSSI is
  non-monotonic in PCB position; near/far separation is set by TX power + cable
  routing, and a weak+distant *central* destabilizes the link (it drives every
  event) — both documented, both handled.

## 7. Evidence pointers

Two repos: (a) **this** repo (`zenoh-pico-ble-test`, branch
`ble5-l2cap-throughput-benchmark`) holds the observer tooling + all
`debug-evidence/`; (b) the **Zephyr controller** (`~/zephyrproject/zephyr`, branch
`fsu-m0`) holds the FSU controller + the on-chip instrument.

- Q1 protocol + matrix: `debug-evidence/observer-q1-20260810/` (also holds the
  frozen `Q2-ACCEPTANCE-PROTOCOL.md`, rev-12).
- Q2 evidence: `debug-evidence/observer-q2-{smoke,symctl1,symctl2,nearfarA1,A2,
  B1,B2}-20260811/`, the ESTABLISHED artifact
  `debug-evidence/gap-proxy-calibration-20260811.json`, and the archived role-swap
  command+output `debug-evidence/observer-q2-roleswap-20260811.txt`.
- Q3a draft protocol: `debug-evidence/observer-q3-20260811/Q3-ACCEPTANCE-PROTOCOL.md`
  (rev-1).
- Tooling: `q2-central/{q2_run.py, analyze_q2.py, combine_calib.py}` (run
  `--selftest` on each).
- FSU controller + on-chip instrument + M0 status (Zephyr `fsu-m0` branch):
  `zephyr-patches/fsu-m0-cheatsheet.md`; `debug-evidence/fsu-m0-20260807/PROVENANCE.txt`.

## 8. Design decisions (resolved with the reviewer, 2026-08-11)

1. **The step is the right estimand; within-connection (mid-step) is the strongest
   implementation.** Primary gate = `|step − 800| ≤ 4 ticks`; the absolute plateaus
   are secondary/diagnostic until the +8-tick offset is understood.
2. **Step-wise cross-validation is primary; absolute readings secondary** (each
   method carries its own offset).
3. **The bit-65 bench shim is acceptable for a scoped bench PHYSICAL claim (not
   conformance)** provided peer participation is independently logged — via a
   peripheral-side FSU completion at the same AA/session (initiator field alone
   insufficient); on-air 0x3B/0x3C capture optional.
4. **One 150 → 100 µs point at 1 M is enough for this M0 claim.** The higher-value
   addition is REVERSE-DIRECTION confirmation (the central turnaround via multi-PDU
   events), NOT a second reduced spacing.
