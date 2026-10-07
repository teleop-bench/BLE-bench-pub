# Investigation scope — the open controller's per-event TX staging

**Thesis to test:** the two open-stack TX puzzles are **one root cause**, not two:
- **Central-downlink refill wall (§11.1):** the sender keeps data ready but only ~10 PDUs reach the
  air per connection event (vs a ~35–41 airtime cap); deep buffers don't move it; `eagain=0`,
  `poolfail=0`. → the host stages only ~10 PDUs to the controller per event.
- **Peripheral-uplink stall (§5):** with the peripheral's TX DLE at 251, `UP sent` freezes at pool
  depth, `txcred` frozen at 64, **zero** segments handed to the controller, uplink = 0. → the host
  stages **0** segments per event.

**The unifying hypothesis:** both are the **host→controller per-event staging path** — the refill
wall is "stages ~10/event," the stall is "stages 0/event." Same curve, two operating points. If
true, one fix lifts the throughput ceiling *and* unblocks the uplink *and* lets FSU convert (FSU is
gated by the refill wall). That is the prize; a narrow "fix the duplex stall" is not.

## What to instrument (behind a Kconfig, like `BT_CTLR_FSU_EVENTFILL_DIAG`)
**Host** (`subsys/bluetooth/host/`):
- `l2cap.c` — `l2cap_data_pull` / `l2cap_chan_le_send_sdu`: count segments pulled per event; watch
  the SDU→PDU seg alloc (does a 251-octet segment fail to alloc on the peripheral?).
- `conn.c` — `tx_processor` / `bt_conn_process_tx` and the **Number-Of-Completed-Packets** handling:
  how many `node_tx` the conn TX queue holds, and how many completions arrive per event (the
  suspected throttle — the host may stage only as many as completed last event).
**Controller** (`ll_sw_split`):
- `lll_conn.c` `lll_conn_pdu_tx_prep` / `isr_done`: how many PDUs the LLL dequeues from `memq_tx`
  and puts on air per event; the ACL-TX buffer accounting; whether it stops early (MD bit) or runs
  out of staged PDUs.

## The discriminating experiment
Add per-event counters at both layers, then run **two configs** and compare the staging counts:
1. Central downlink, 480 B, 15 ms (the refill-wall case) → expect host-stages ≈ controller-pulls ≈ ~10.
2. Peripheral uplink, DLE=251 (the stall case) → expect host-stages = 0 (or controller-pulls = 0).

Then the decisive question: **is the per-event stage count bounded by the Number-Of-Completed-
Packets feedback?** Test by forcing more completions / a deeper `BT_CONN_TX_MAX` + logging the
completion cadence. If stage-count tracks completions-per-event in *both* cases, they share the root.

## Candidate root causes (ranked)
1. **Per-event staging bounded by completion feedback.** The host stages ≈ (PDUs completed last
   event). Completions arrive once per event (~10 for the central); for the stalled peripheral, 0
   sent → 0 completed → 0 staged → deadlock. *Single mechanism for both — the leading hypothesis.*
2. **DLE=251 buffer/MPS mismatch on the peripheral TX path** — a max-size segment fails to alloc or
   fragment in the peripheral role (central downlink at 251 works, so role-specific). Explains the
   stall but not the refill wall.
3. **Credit/`txcred` accounting** — the peripheral's granted TX credits never consumed because the
   send is gated on a condition that never fires. Explains the stall; would need a separate cause
   for the wall.
4. **LLL per-event scheduling / MD-bit early close** — the controller ends the event before filling
   it. (§9.6 showed the FSU diag hurts this, but gated off it still caps ~10 → not the whole story.)

## Outcomes & value
- **If shared root (hyp. 1):** highest value — one fix raises the throughput ceiling, unblocks
  uplink, and unlocks FSU conversion. Likely an upstream Zephyr host/controller contribution
  (open nRF54L L2CAP TX). Would lift **every** number in the benchmark.
- **If distinct:** still yields a peripheral-TX-at-full-DLE bug report (per the 08-14 note, likely
  first) and a characterised refill limit — both worth publishing.
- **Not** on the critical path for the teleop safety-link decision (asymmetric link; tiny control
  echo doesn't hit either). This is an ecosystem/ceiling-raising investigation, not a product blocker.

## Effort
~1 day: add 4 gated counters (2 host, 2 controller), 2 measurement runs, correlate. If shared-root
confirmed, the fix + upstream PR is a further multi-day controller/host task. Pinned 15 ms, fresh
reset, `analyze.py` + a new per-event-staging parser.

## Derisking — staged go/no-go (added after review)
The risk isn't the instrumentation; it's (a) discovering it's already known/fixed upstream *after*
spending the day, and (b) the shared-root thesis being wrong (one prize → two narrow fixes). Cap
both cheaply:

**Stage 0 — zero-bench upstream check (~1 hr).** `git log v4.4.1..main` on `host/l2cap.c`,
`host/conn.c`, `ll_sw/nordic/lll/lll_conn.c`; search Zephyr/Nordic issues for the peripheral-TX-at-
full-DLE stall and the per-event-staging / Number-Of-Completed-Packets throttle. Outcome: known/
fixed → adopt or kill; not found → confirms novelty. **Gate:** if already fixed upstream, stop.

**Stage 1 — config-only probe + reproducibility (~½ day, no code).** Sweep `BT_CONN_TX_MAX` and
interval; read PDU/event from the existing (perturbing) event-fill diag. Deep *buffers* already
don't move it; if `CONN_TX_MAX`/completion-cadence knobs don't either, the cap is controller
radio-scheduling, not host staging → **half-answers shared-root for free**. Confirm the refill wall
(~10 PDU/event) is stable across runs at a pinned config; lock it. **Gate (kill shared-root):** if
the host would stage ≫10/event but the controller still airs ~10, it's a controller cap unrelated
to the stall's host-staging mechanism → two separate problems, stop the unified effort.

**Stage 2 — cheap, non-perturbing instrumentation first (~½ day).** Host counters
(segments-pulled/event in `l2cap_data_pull`, completions/event in `tx_processor`) before the
controller ISR histogram (which we KNOW throttles CoC, §9.6 — a live measurement-artifact risk).
Best of all: an on-air *following* sniffer counts PDUs/event with **zero DUT perturbation**.

**Scope reframe:** the **refill wall is the prize** (stable, caps throughput, gates FSU); the
**peripheral stall is a niche, config-flaky bonus** (duplex, which the safety link doesn't need).
Instrument the wall first; treat the stall as a confirmatory check — never let it drive scope.

## Stage 0 RESULTS (2026-08-25) — GATE: PROCEED
Local diff (fsu-m0 @ v4.4.1-15) vs `origin/main` @ e201b84b04e (2026-07-31), plus a web sweep of
Zephyr issues/PRs/DevZone.

**Already fixed upstream? No — nothing to adopt, nothing kills the investigation.**
- Controller LLL `lll_conn.c`: **zero** commits on `origin/main` since v4.4.1. The per-event TX
  pipeline we suspect is byte-for-byte the same on current Zephyr → the refill wall persists upstream.
- Host `l2cap.c`/`conn.c`: only cosmetic `net_buf_take/drop` refactors (RX/SDU path) + a new
  `le_param_update_rejected` cb + BR/EDR work. **No** change to the credit gating, `l2cap_data_pull`,
  or the completed-packets staging. No merged PR reworks per-event TX or adds a "stage more/event" knob.

**Novelty — confirmed; the diagnosis is unreported.**
- The *mechanism* is documented: Zephyr's LE-host docs say `BT_BUF_ACL_TX_COUNT` bounds how many
  packets the host may have outstanding in the controller **before it must wait for the HCI
  Number-Of-Completed-Packets (NoCP) event** — exactly our suspected throttle. But **no issue
  diagnoses this as the per-event ceiling**, and none names a ~10-PDU/event or ~150 KB/s nRF54L cap.
- SoftDevice contrast (DevZone): SDC **fills the event by time-budget** (not a per-event packet
  count) → supports that our wall is open-controller/host-pacing-specific, not fundamental.

**Two historical issues that RHYME — cross-check these:**
- **#20153 "BLE small throughput"** (CLOSED, no root cause): only ~2 PDU/event at 7.5 ms/251 B/2M
  when ≥5 feasible — the *same refill-wall symptom* on an older stack. If our NoCP diagnosis is
  right, it explains #20153.
- **#21854 "ACL data packets with 251 bytes not acknowledged"** (CLOSED): a **251-byte ACL never
  gets a NoCP event** (250 works) → TX stalls at the max-DLE boundary. This is the best match to our
  peripheral "251 freezes, 27 flows" signature — **and it points at NoCP non-delivery**, the *same*
  mechanism as the refill wall. **Independent support for the shared-root thesis:** stall = "NoCP
  never arrives → 0 staged"; wall = "NoCP arrives once/event → ~10 staged." Same feedback loop, two
  operating points — which is precisely what the investigation set out to prove.
- (#42761 peripheral DLE stuck at 27, #46692 new-LLCP −50%, #69975 RX-credit=1 → seg_recv: adjacent,
  not matches.)

**Bonus — a ready instrumentation hook.** `origin/main` `l2cap.c:934` has a `__weak
bt_test_l2cap_data_pull_spy(conn, chan, amount, length)` on the exact pull path we'd measure — a
lower-perturbation Stage-2 counter site than patching `l2cap_data_pull` by hand.

**Decision:** Stage-0 gate **PASSES** (not fixed upstream, novelty holds). Proceed to Stage 1
(config-only NoCP/`CONN_TX_MAX` probe + reproducibility). The #21854 NoCP-boundary lead sharpens the
Stage-1/2 focus onto **when the NoCP event fires** for the 251-byte case vs the ~10/event case.

## RESULTS (2026-08-26) — thesis FALSIFIED, cleanly
Ran Stage 1 (config-only) → Stage 1b (FSU on/off) → Stage 2 (host-handed counter) → Stage 2b
(stall intermittency). Full data + logs: `debug-evidence/tx-staging-20260826/FINDINGS.md`.

**Downlink "refill wall" is the tIFS-bounded AIRTIME wall — not a staging throttle** (scope: tested
7.5–25 ms / 2M / 251 B). **Three converging observations, anchored by the exact capacity match:**
- **Anchor — exact packet-capacity match:** measured `max = 5 / 10 / 17` pairs at 7.5 / 15 / 25 ms
  equals `floor(interval / 1392 µs)` exactly (251-octet PDU ≈ 1048 µs @ 2M + 44 µs empty ACK + 2×tIFS);
  FSU (tIFS→52 µs, cycle ~1196 µs) packs 12 vs 10 pairs at 15 ms → **+20% integer ≈ measured +18.6%**.
  This is the load-bearing evidence.
- **Interval sweep (converges):** aired PDU/ev = 4.87 / 9.64 / 16.30, throughput flat ~155 KB/s;
  deep(64) ≡ shallow(8) at 15 ms. *Caveat: a fixed time-rate staging path would also give ∝ interval —
  scaling alone doesn't discriminate; the capacity match does.*
- **Host-handed counter (converges):** `host_pulls/ev ≈ aired/ev` (9.68≈9.69). *Caveat: the hook
  fires when the lower path requests data, so it shows pulled packets air — not that the controller
  would have requested more had airtime remained.* ("~35–41 wall" was a ~50 ms figure.)
- **FSU on/off @15 ms** ran **ABAB** (ON/OFF/ON/OFF — order-counterbalanced but *not* drift-cancelling ABBA).

**Peripheral uplink at DLE=251 stages normally in the four opens tested; the stall did not reproduce.**
uplink flowed (~87 KB/s duplex), `host_pulls ≈ UP sent`, `txcred=0`; the documented `UP sent`-frozen /
`txcred`=64 stall did NOT trigger — intermittent, not a deterministic DLE=251 effect, and no
*persistent* throttle in those opens (not a proof that none can occur).

**Verdict:** the shared-root premise ("one per-event TX-staging fix lifts the whole ceiling")
dissolves *within this regime*. No persistent host-staging throttle was observed in either direction
across the tested opens. The ceiling is **airtime** (moved only by FSU / PHY / packing), and the
"stall" is a separate, elusive bug. **No multi-day
controller-internals fix is warranted** — exactly the outcome the Stage-0/derisking gates were
built to reach cheaply before over-investing. Net cost: ~1 evening of config sweeps + one 2-line
gated host counter, no speculative rewrite.

**Corrections propagated:** `coc-technical-overview.md §11.1` (refill-limit framing → airtime) and
the FSU "3/4-empty event" language; memory `ble5-l2cap-throughput-benchmark` + `gatt-vs-coc-teleop`.

## Relation to upstream Zephyr issues (Stage 0 cross-check, revisited with the result)
The result **produces no new Zephyr bug to file** — and it recontextualizes the Stage-0 hits:
- **#20153 "BLE small throughput"** (~2 PDU/event at 7.5 ms, closed no-root-cause): **explained, not
  a bug.** ~2 PDU/event at 7.5 ms is on the same airtime line we measured (4.87 at 7.5 ms with 480 B
  / 251-octet PDUs; smaller/older-stack configs land lower). It's airtime, not a defect.
- **NoCP-pacing hypothesis** (host staging bounded by Number-Of-Completed-Packets, from the
  `BT_BUF_ACL_TX_COUNT` docs): **moot.** The host hands exactly what airs (1:1), and deep ACL_TX
  doesn't raise the ceiling — the binding constraint is airtime upstream of any NoCP feedback.
- **#21854 "251-byte ACL never gets a NoCP event"** and the peripheral DLE=251 stall: **could not be
  reproduced**, so there is nothing to file. Our probe flowed at DLE=251 across 4 channel-opens.
  If it recurs, #21854 remains the closest rhyme, but a filing needs a deterministic repro we don't have.
- **#46692 (new-LLCP −50%), #69975 (RX credit=1 → seg_recv):** unrelated to this ceiling.

**Bottom line for upstream:** nothing to contribute as a *bug*. The contribution is *documentation* —
a measured, reproducible characterization that open nRF54L L2CAP throughput is tIFS-airtime-bound at
the 12.5–15 ms peak (levers: FSU / PHY / packing), which the ecosystem lacks. That belongs in our
open benchmark, not a Zephyr issue tracker.

## DLE=251 stall — config-level suspects RULED OUT (2026-08-26, no bench needed)
The doc read surfaced two concrete host-config hypotheses for the intermittent peripheral-uplink
stall. Both are eliminated from config/signature alone:
- **Host fragmentation** (if `BT_BUF_ACL_TX_SIZE` < 251, every 251-octet PDU fragments onto
  `BT_L2CAP_TX_FRAG_COUNT=2`, whose help warns of deadlock): **FALSIFIED** — both ends already set
  `BT_BUF_ACL_TX_SIZE=251` (source + effective `.config`), so no host fragmentation occurs at DLE=251.
- **RX-credit starvation** (`L2CAP_LE_MAX_CREDITS = BT_BUF_ACL_RX_COUNT-1` too low): **inconsistent
  with the signature** — the documented stall is `txcred` **frozen at 64** (credits granted, never
  consumed), the opposite of starvation (which shows low/zero credits). The peripheral had ample
  credits and still sent nothing.
So the config-reducible suspects are exhausted. The residual stall is an **intermittent race** on the
peripheral TX path (app→controller handoff) that did not reproduce in 4 channel-opens and is not a
config bug — catching it needs a reconnect-cycling harness, tracked as a separate low-priority item.
