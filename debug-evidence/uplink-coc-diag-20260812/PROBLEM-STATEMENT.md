# Problem statement (for external review) — L2CAP CoC peripheral→central TX stalls on a CENTRAL's FIRST connection after a (re)boot; a same-boot reconnect recovers. NOT the teardown mode, NOT connection ordering.

**Status: isolated to the central's fresh-boot lifecycle; teardown-mode and Order-A triggers
RETRACTED; mechanism not root-caused; diagnostic-only. No fix claimed.** Single-connection
throughput is healthy; the defect is a peripheral→central CoC TX stall (`submitted=8,
completed=0`, sink 0) on the session that follows a **central reboot**, whereas a **same-boot**
reconnect after the identical loss recovers. Controlled tests show the trigger is **independent
of teardown mode** (0x08 ≡ 0x13), **connection ordering** (a rebooted central stalls even
central-first/Order B), and **reconnect settle time** (stalls with a 5 s settle). Leading
hypothesis: **a central's first CoC connection after a (re)boot wedges the peripheral uplink;
later same-boot connections stream.**

> **RETRACTION (2026-08-12):** an earlier revision of this document concluded the **abrupt
> teardown MODE** (0x08 supervision timeout) was the trigger. That was **WRONG** — a
> confound flagged in review: the abrupt cells *reset the central* (fresh boot) while the
> graceful cell reconnected *same-boot*, so teardown mode was confounded with central
> lifecycle. A controlled test (ONE central binary, runtime-selected teardown; `deconfound-v2/`)
> shows: with the sender held fully drained and session 1 healthy, **graceful+reboot (0x13)
> STALLS (2/2)** just like **abrupt+reboot (0x08) STALLS (2/2)**, while **graceful+same-boot
> (0x13) RECOVERS (2/2)**. So **the central REBOOT — not the teardown mode — determines the
> outcome**, and the reboot matters because it makes the central rejoin *after* the peripheral
> is already advertising (Order A). Teardown mode (0x08 vs 0x13) is irrelevant.

Three bounded controls remain valid: (i) reuse of the same channel object is **not necessary**;
(ii) a same-boot graceful reconnect recovers; (iii) sender saturation at teardown is ruled out
(the stall reproduces with `outstanding=0, pool_wait=0`). The corrected localization and the
remaining open mechanism are in "Localized conclusion" and `deconfound-v2/FINDINGS-v2.md`.

## Environment
- 2× Nordic **nRF54L15-DK** (`nrf54l15dk/nrf54l15/cpuapp`).
- Open **Zephyr v4.4.1** + a controller branch derived from the open M0 FSU work
  (`fsu-m0` HEAD `9999e040446`) — un-merged/reference; **not** a general conformance claim.
- **L2CAP CoC**, PSM `0x0080`, **2M** PHY, 7.5 ms interval, effective data length **251**,
  **244-byte** SDUs.
- **Peripheral** = uplink blaster = L2CAP **server**; TX `net_buf` pool = **8**. The
  committed app reuses a single static `bt_l2cap_le_chan` across sessions, but the
  discriminator (below) shows that detail is **not** causal — a fresh, never-used channel
  object fails the same way, so the failure is a property of the reconnected session, not
  the object.
- **Central** = uplink sink = L2CAP **client** (`bt_l2cap_chan_connect`), `seg_recv` credit sink.
- Apps `z54-uplink-dk` / `z54-uplink-central`, instrumented: monotonic
  submitted/completed/send_failed/pool_wait/released counters, **sink-delivered** bytes,
  per-boot run id + the controller's **shared connection AA** for paired logs.

## Observed behavior
- **First connection (channel object used once): works.** Sustained **~153 KiB/s
  sink-delivered**, `submitted≈completed` (outstanding bounded ~10–11), `pool_wait=0`,
  0 disconnects — validated end-to-end, paired sink/source on the same AA.
- **The 2nd+ connection after an abrupt loss stalled its TX in every instrumented
  attempt** (independent of the channel object — see discriminator). Across **three**
  instrumented saturated reconnect attempts, each stalled for the remainder of its
  **20–30+ second** observation window and recovered only after **reboot** in the tested
  workflow (the archive proves these reconnect failures, not every possible reconnect or
  indefinite permanence). On the reconnected session:
  - `submitted=8` — the 8 pool buffers are accepted by `bt_l2cap_chan_send`; then
  - `completed=0` — `.sent` **never** fires; `outstanding=8`;
  - `pool_wait` rises monotonically throughout the observation window (subsequent
    `net_buf_alloc` time out — pool exhausted, buffers never returned); and
  - the **sink receives 0 bytes** for that session.
  So for those SDUs **no sender completion (`.sent`) and no sink delivery were observed.**
  (No RF capture was taken, so transmitted-but-unacknowledged packets remain possible;
  Zephyr defines `.sent` as implementation-dependent controller completion, not literal
  on-air delivery.) Observed identically as `sub=8 cmp=0 out=8`, `poolwait` → 273 … 471,
  sink `total=0`; the sender did not recover within the observation window (reboot-recovered).

## Reproduction (corrected — the trigger is an Order-A reconnect, produced by a central reboot)
1. Establish the CoC (central up first, peripheral joins = Order B; sink streaming ~153 KiB/s).
2. Cause the central to **reboot** while connected (reset/re-flash it, or self-`sys_reboot`).
   The peripheral loses the link and re-advertises; the freshly-booted central then joins an
   already-advertising peripheral = **Order A**. (The teardown reason — abrupt 0x08 or graceful
   0x13 — does **not** matter; only whether the central rebooted.)
3. The reconnected (Order-A) session stalls: `submitted=8, completed=0`, sink 0, `pool_wait`
   rising monotonically; recovers only after the workflow re-establishes Order B. A **same-boot**
   reconnect (central stays up) does NOT stall. Equivalently, boot the peripheral first so it
   advertises before the central scans — the very first session stalls the same way, with no
   teardown at all.

## Ruled OUT as the cause (each tried; wedge persists)
- **Setup-procedure timing / race.** Serialized bring-up so the CoC opens ONLY after 2M
  is confirmed AND effective TX/RX length = 251 (removed the timer-based premature open; a
  watchdog retries the missing PHY/DLE step). Log confirms `l2cap_chan_connect … (after
  2M + eff251)`. Still wedges.
- **Channel-object reuse hygiene.** Gated reuse on `.released` (object marked free only in
  the `.released` callback) and REMOVED the previous premature `memset` of the static
  channel. `.released` is observed to fire before reuse. Still wedges.
- **FORCE_MD asymmetry (partly substantiated).** The central carried a leftover
  `CONFIG_BT_CTLR_FORCE_MD_COUNT=10` (an nRF52 hack; note the COMMITTED
  `z54-uplink-central/prj.conf` still reads `=10`) vs the peripheral's `=0`. A rebuild with
  **both = 0** (overlay `configs/nofmd.conf`; resolved `.config` snippets archived under
  `stage3-4/`) still stalled the reconnected session on the BLASTER side (`sub=8 cmp=0`,
  `poolwait` → 471). Caveat: the FORCE_MD=0 run captured the blaster log only (no paired
  sink log), so this is "not resolved by FORCE_MD=0," slightly weaker than the other
  ruled-out items.
- **Instrumentation artifact.** An earlier negative "inflight" counter was a probe bug
  (decremented in `.sent`, never incremented at send); replaced with correct
  submitted/completed counters. The wedge is REAL (sink-delivered = 0, not a missed `.sent`).

## Discriminator results (five bounded controls; `discriminator/`, `deconfound/`, `deconfound-v2/`, `deconfound-v3/`)
1. **Reuse of the SAME channel object is not necessary for the failure; stale state
   confined to the old object is ruled out.** A firmware variant with two preallocated,
   zero-initialised channel objects hands session 1 = object A, session 2 = **object B
   (never used before)**, session 3 = A (reuse); neither is `memset`. After an ABRUPT drop:
   session 1 (A) streamed (47 121 SDUs, sink delivered 11.35 MB), but session 2 on the
   **FRESH object B** stalled identically (`sub=8 cmp=0 out=8`, sink session-2 delivery 0),
   as did session 3 (A reuse).
2. **A graceful-disconnect companion recovers and sustains full throughput.** The central
   issues `LL_TERMINATE` (reason 0x13/0x16) then reconnects: session 1 streamed and session
   2 on the **fresh object B streamed ~155 KiB/s** (sink session-2 delivery **≈ 3.63 MB /
   3.46 MiB**; the sink `total` counter is cumulative — 1 526 220 B before session 2,
   5 152 792 B after).

3. **DECONFOUND v1 (sender condition) — sender saturation at teardown is RULED OUT.**
   (`deconfound/`.) A producer-pause drives the blaster to a fully drained/healthy state
   (`outstanding=0`, `pool_wait=0`, `submitted==completed`) BEFORE teardown; the stall still
   reproduces, so the sender being backlogged at teardown is not the cause. *(This control's
   own top-line "abrupt mode is the trigger" is superseded by v2 below — it too varied central
   reboot vs same-boot.)*
4. **DECONFOUND v2 (central lifecycle) — the CENTRAL REBOOT, not the teardown mode, is the
   trigger.** (`deconfound-v2/`.) ONE central binary with a runtime-selected teardown, every
   cell with a healthy+drained session 1, n=2 each:
   - **abrupt+reboot (0x08)** → session 2 **STALLS 2/2** (s1 `sub=cmp=5081/4832 out=0 poolwait=0`);
   - **graceful+reboot (0x13)** → session 2 **STALLS 2/2** (s1 `sub=cmp=5221/5115 out=0 poolwait=0`);
   - **graceful+same-boot (0x13)** → session 2 **RECOVERS 2/2** ~150 KiB/s (s1 `sub=cmp=5095/4903`).
   All six runs independent (distinct AAs/bytes; an initial archiving error that duplicated the
   a/s run2 files was corrected with fresh reset-isolated runs — see `deconfound-v2/FINDINGS-v2.md`).
   **g vs s** (both 0x13; differ only in reboot) → reboot STALLS, same-boot RECOVERS ⇒ the
   **central reboot** is the trigger. **a vs g** (both reboot; differ in teardown mode) →
   both STALL ⇒ **teardown mode is irrelevant.**
5. **DECONFOUND v3 (ordering) — a REBOOTED central stalls even in central-first (Order B)
   order; Order-A is NOT the mechanism.** (`deconfound-v3/`.) A `SCAN-READY`-gated peripheral
   forces a rebooted central to reconnect **central-first (Order B)**: session 1 healthy
   Order-B and drained (`sub=cmp` 4928/4879/4612/4756), mode-g teardown → central reboots →
   Order-B reconnect. Session 2 **STALLS 2/2** immediate, and **STALLS 2/2** even with a **5 s
   settle** before reconnecting (`orch8s.py`). So neither **ordering** nor **settle time**
   rescues a rebooted central ⇒ the **central reboot / fresh host+controller lifecycle** is
   the trigger, independent of ordering. Order-A (the v2 leading hypothesis) is **refuted.**

## Localized conclusion (corrected — the fresh-central lifecycle is the trigger; Order-A refuted)
The peripheral→central CoC TX stall is triggered by the **central reboot (fresh host+controller
lifecycle)**. With session 1 healthy+drained: reboot STALLS and same-boot RECOVERS (v2), and a
rebooted central STALLS even when forced to reconnect **central-first (Order B)** and even with
a **5 s settle** (v3). So the trigger is **independent of teardown mode, connection ordering,
and reconnect settle time**. **Leading unified hypothesis:** *a central's FIRST L2CAP CoC
connection after a (re)boot wedges the peripheral uplink; subsequent same-boot connections
stream.* A reboot resets this (post-reboot reconnect = 1st-since-reboot → stall); a same-boot
reconnect (2nd+ since boot) recovers. This also explains the reboot-free "peripheral-first"
first-session wedge (the central is likewise on its 1st-since-boot connection) and why the
"healthy Order-B session 1" runs each had a throwaway/stale connection first. The failure is
**peripheral sender-path state on the central's first-since-boot CoC** (`submitted=8,
completed=0`, sink 0); the mechanism is **not** root-caused — open between host L2CAP
TX-queue/credit and controller TX-context initialization on that first connection
([#46073](https://github.com/zephyrproject-rtos/zephyr/issues/46073)-adjacent). **Retracted:**
the abrupt-teardown-mode trigger (v1) and the Order-A mechanism (v2 hypothesis). **Ruled out:**
sender saturation at teardown (v1), same-channel-object reuse (control 1), connection ordering
and reconnect settle time (v3).

## Remaining tests (mechanism + scope)
- **Reboot WITHOUT Order-A** — **DONE (`deconfound-v3/`)**: a rebooted central STALLS even
  central-first (Order B) and even with a 5 s settle ⇒ fresh-central lifecycle, not ordering.
- **Confirm the "first-CoC-since-boot" hypothesis directly**: after a central reboot, let it
  complete one throwaway connection, then reconnect a second time (same boot) — predict recover.
- **Reverse roles** (central = server / peripheral = client) to test role-specificity.
- **Downlink direction** under the same post-reboot reconnect.
- Instrument host L2CAP TX-queue/credit and controller TX-context initialization on the
  central's first-since-boot CoC to identify which state is wrong.

## Impact / scope
Single-session uplink throughput is healthy (~153 KiB/s). The defect is a **peripheral→central
CoC TX stall on the central's first connection after a (re)boot**. Because an abrupt link loss
(Wi-Fi-failover RF blip → supervision timeout) may be *followed* by a central restart, this is
a **robustness concern for the uplink CoC path** as a failover transport — but note the trigger
is the **central restart**, NOT the abrupt loss itself and NOT the reconnect ordering: a
same-boot reconnect after the identical loss recovers. Localized to peripheral sender-path
state on the central's first-since-boot CoC, independent of the channel object, teardown mode,
connection ordering, and settle time; mechanism not root-caused; no fix asserted. (Downlink
direction untested for the same behavior.)

## Evidence
`debug-evidence/uplink-coc-diag-20260812/`: `stage1-smoke/` (first exposure, paired
counters), `stage3-4/` (serialized-startup + release-gated + FORCE_MD=0 runs, all still
stalling on reconnect), `discriminator/` (two-object + graceful-companion paired logs),
`deconfound/` (v1 producer-pause: sender-saturation-at-teardown ruled out; its own
"abrupt-mode" top-line **superseded** by v2), **`deconfound-v2/` (ONE central binary,
runtime-selected teardown: abrupt+reboot STALL ×2, graceful+reboot STALL ×2, graceful+same
-boot RECOVER ×2 — central reboot is the trigger, teardown mode irrelevant; paired logs +
`orch5.py` + firmware HEX/ELF/.config + `FINDINGS-v2.md`)**, **`deconfound-v3/` (SCAN-READY
-gated periph forces a rebooted central to reconnect central-first: STALL ×2 immediate + STALL
×2 with 5 s settle — ordering & settle ruled out, fresh-central lifecycle isolated; paired logs
+ `orch8.py`/`orch8s.py` + firmware HEX/ELF/.config + `FINDINGS-v3.md`)**, `configs/` (FORCE_MD
provenance), and `README.md` / `stage1-smoke/FINDINGS.md`.
