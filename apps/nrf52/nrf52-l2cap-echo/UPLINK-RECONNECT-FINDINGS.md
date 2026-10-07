# BLE 5 L2CAP CoC — uplink throughput + reconnect robustness (Zephyr 4.4.1)

Measured 2026-07-30 on the open `ll_sw_split` controller (no SoftDevice),
nRF52840 dongle + nRF52832 DK, 2M PHY / DLE-251 / 7.5 ms. Firmware:
`z44-uplink-dk/` (peripheral sender) + `z44-uplink-central/` (central `seg_recv` sink).
This began as a re-test of what we'd been calling "#46073" — see the **citation
correction** at the bottom; that issue number was wrong.

## 1. Uplink vs downlink throughput — NO asymmetry (once the peripheral DLE is fixed)

Same 244-byte SDU, same link, opposite direction:

| Direction | Sender role | Throughput |
|---|---|---|
| **Downlink** (controller→robot) | central | **~150–155 KB/s** |
| **Uplink** (robot→controller) | peripheral | ~43 → **~152 KB/s (FIXED)** |

### ⚠️ CORRECTION (2026-07-30): the "~3.5× asymmetry" was OUR FIRMWARE BUG, not BLE

We initially measured uplink ~43 vs downlink ~150 and (wrongly) attributed the 3.5×
gap to a *structural* peripheral→central limit (central controls the connection-event
length; ~1.35 packets/event). **That was misdiagnosed.** The real cause: the
**peripheral never called `bt_conn_le_data_len_update()`**, so its TX data length
stayed pinned at the **27-octet DLE default** (the central's DLE request only raises
the *central's* TX = the downlink). On-air the peripheral was sending 27-byte LL PDUs
— ~34% header overhead vs ~6% for 251-byte PDUs.

**One-line fix** (peripheral requests DLE MAX in its `connected()` callback):
`bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX)` → `tx_max` 27 → 251 →
**uplink ~43 → ~152 KB/s**, i.e. ≈ the downlink. Measured, not inferred. This matches
arXiv 2606.21270 (nRF54L15 peripheral→central ~127 ≈ central→peripheral ~131,
near-symmetric) — the research was right; there is **no fundamental uplink penalty**.

**Retractions** (things earlier drafts/notes claimed that are now FALSE):
- ❌ "~3.5× uplink asymmetry is structural / an open-`ll_sw_split` limit." It's a
  missing peripheral DLE call; uplink ≈ downlink (~150) once fixed.
- ❌ "Peripheral limited to ~1.35 packets/event." Misdiagnosed — it was PDU *size*
  (27 B), not packet count.
- ❌ "Central-side FORCE_MD inert / SLOT_RESERVATION already maxed → ~43 is the
  ceiling." Those observations were real but irrelevant; the ceiling was the
  peripheral's own un-negotiated TX DLE.
- ❌ "Make the robot the central to get a fast uplink." **Unnecessary** — the
  robot-*peripheral* gets ~150 uplink with the one-line DLE fix. No role inversion,
  no one-dongle-per-robot needed for bandwidth.

**Architecture implication (corrected):** both directions reach ~150 on the open
nRF52 stack. A robot as BLE **peripheral** can stream ~150 KB/s uplink AND receive
~150 downlink — provided its firmware requests DLE on both sides of the link. The
fleet-star (relay = central, robots = peripherals) is fine for bandwidth.

### GATT vs CoC — measured on 4.4.1 (2026-07-30)

Built GATT throughput tests (`z44-gatt-dk`/`z44-gatt-central` = notify uplink;
`z44-gattdl-dk`/`z44-gattdl-central` = write-without-response downlink), same rig /
244-byte payload / 2M-DLE-7.5ms / ATT MTU 247. **Full 2×2, all measured:**

| | Uplink (peripheral→central) | Downlink (central→peripheral) |
|---|---|---|
| **L2CAP CoC** | ~43 → **~152** (peripheral DLE fix) | ~150 |
| **GATT** | ~44 → **~154** (peripheral DLE fix) | ~155 (write-no-resp) |

Findings: (a) **CoC ≈ GATT in both directions** on 4.4.1 (matches 2.7). (b) **No
uplink/downlink asymmetry** once the peripheral requests its own DLE — the initial ~43/~44
uplink was the `tx_max=27` bug (see §1 correction), identical on both transports. GATT
uplink fix measured (~44→~154), not assumed. **GATT footgun still applies:** use Notify /
Write-Without-Response (fire-and-forget); the ack'd variants (Indication / Write-With-
Response) are round-trip-bound and much slower — L2CAP CoC has no such trap. GATT
*reconnect* behavior after a central reboot was **not** tested (CoC-specific credit wedge;
GATT's analogue is CCC-subscription loss).

## 2. Reconnect robustness — the important finding

Tested peripheral-uplink recovery across link drops. Instrumented the sender with a
completion-paced window and an `inflight` counter (SDUs handed to `bt_l2cap_chan_send`
but not yet `.sent`) to probe buffer reclaim.

| Regime | Result |
|---|---|
| **Sustained streaming** | ✅ Healthy — minutes at ~43 KB/s (pre-DLE-fix rate; ~152 after the §1 fix), no wedge. The within-session TX-pool-exhaustion wedge (present on 2.7) is **healed** — consistent with the 3.7 host-TX-path rewrite (removed the BT-TX thread + HCI-fragment/L2CAP-segment pools; 3.7.0 release notes). *Causal link is our observation, not documented.* |
| **Advertising restart after disconnect** | ⚠️→✅ `bt_le_adv_start()` returned **-ENOMEM** when called synchronously in `disconnected()` — a **documented** Zephyr foot-gun (Connection Management docs: the closed conn isn't reaped yet; remedy = restart via `k_work` and/or raise `CONFIG_BT_MAX_CONN`). Fixed with workqueue-deferred restart + `BT_MAX_CONN=2`. |
| **Clean link-drop, central stays alive** | ✅ **Full recovery, repeatable.** Self-triggered `bt_conn_disconnect()` (reason 0x16), central not rebooted: uplink resumes ~43 KB/s (pre-DLE-fix rate) **every cycle (4/4 tested)**, `blocked-events=0`. |
| **Central power-cycle (reboot + abrupt USB loss)** | ❌ **TX-wedge.** Reconnects at GAP + L2CAP level ("CoC up"), but the reconnected channel produces **zero TX completions** — `.sent` never fires; new sends queue (`inflight` pins at the window) and never transmit. `inflight` at the *prior* disconnect (supervision timeout, 0x08) also never drained → in-flight SDUs not reclaimed. |

**Completion-paced sender: necessary but NOT sufficient.** A bounded in-flight
window (driven by `.sent`) is the API-idiomatic pattern and costs **zero throughput**
(held full ~43, the pre-DLE-fix rate). It does **not** rescue the power-cycle case, because the failure is
*zero completions*, not *too many in flight* — no app pacing fixes zero completions.

### Corrected attribution

The wedge is **NOT triggered by reconnection per se.** A clean link-drop with a
persistent central recovers fully and repeatedly. The wedge reproduces only on the
**central power-cycle** (reboot + abrupt USB loss). This substantially de-risks the
failover story: a link that flaps while the teleoperator relay stays up
re-establishes the uplink on its own; the narrow danger is the **relay restarting**.

### Isolation follow-up (2026-07-30): the central REBOOT is the trigger

Ran a graceful-disconnect-**then-central-reboot** test (central `bt_conn_disconnect`
with reason 0x13, then `sys_reboot()` from its disconnected callback; DK self-disconnect
disabled). Result: **wedges every cycle** — reconnected DK shows `inflight=4`, zero
`.sent`, 0 KB/s. The disconnect reason is **0x13 (graceful)**, not 0x08.

| Loss type | Central | Reconnected uplink |
|---|---|---|
| graceful (0x16) | **alive** | ✅ recovers (4/4) |
| **graceful (0x13)** | **reboots** | ❌ **wedges** |
| abrupt (0x08) | reboots + USB churn | ❌ wedges |

**Isolation:** the only variable that flips graceful+alive (✅) into a wedge is the
**central reboot** — not abruptness, not USB churn. Therefore the realistic failover
cell — **robot out of range (abrupt timeout) while the relay stays alive** — should
**recover** (no reboot involved). Strong inference (reboot cleanly isolated as cause),
though abrupt+alive was not directly produced (needs physical RF separation). **Good
news for failover:** link flaps with a persistent relay self-heal; the narrow danger
is the **relay process restarting**.

**Mechanism (sharpened hypothesis):** central-alive keeps a *consistent* L2CAP channel
context across reconnect → works. Central-reboot reconnects with **fresh** channel/credit
state, but the **DK peripheral reuses its static `le_chan`** carrying **stale** state
(stranded in-flight buffers + old credit/TX-queue) → TX jams (new sends queue, never
complete). A **peripheral-side stale-channel-state** issue surfaced by a fresh central.
Directly testable fix: reinitialize `le_chan` on disconnect / use a fresh channel per
connection.

**Fix attempt (2026-07-30): `memset(&le_chan, 0, …)` in `l2cap_accept` — DID NOT WORK.**
Still wedges. The tell: each reconnect sends **exactly 976 bytes = 4 × 244** (the whole
window) then zero — the SDUs are *accepted* (`chan_send` returns success, queued) but
**never transmit / never complete** (`.sent` never fires). So the cause is **not**
app-level stale `le_chan` state; it's deeper — a **TX-credit / connection-level
re-handshake failure** when a freshly-rebooted central reconnects to a peripheral that
did not reboot. The DK queues SDUs it has no (granted) credits to send. This is a
**stack-level issue, candidate for a minimal repro + Zephyr issue**, not app-fixable.
**Practical mitigation for the failover adapter:** watchdog the uplink — if no `.sent`
completions for ~2 s after a reconnect, force a full teardown (peripheral-side
`bt_disable()`/`bt_enable()` or disconnect+re-advertise) to get a clean channel. Since
the trigger is narrowly the *central reboot* (link-flap-with-live-relay recovers), this
is an edge case a relay-restart watchdog can cover.

**Is this L2CAP-only? What about GATT?** The *wedge mechanism* is L2CAP-CoC-specific
(the credit-based channel + `le_chan`); GATT notifications don't use CoC credits, so
this exact failure likely won't map. BUT GATT has its own central-reboot concern: the
**CCC subscription** (Enable Notifications) is lost on a non-bonded central reboot, so
the peripheral won't notify until the central **re-subscribes** — different mechanism,
same practical need ("central reboot ⇒ re-setup"). (The ~43 up / ~150 down "asymmetry"
was the peripheral DLE bug retracted in §1, not a transport or role property.) GATT reconnect behavior is **untested here** — worth a run if
GATT becomes a candidate transport.

## Citation correction (important)

Prior notes cited **Zephyr #46073** as "the peripheral→central TX-context wedge."
**That is wrong.** GH #46073 is *"IPSP (IPv6 over BLE) example stop working after a
short time"* — an IPSP-sample bug, **closed as stale (2022)**, not our L2CAP-CoC case.
Corrected lineage for peripheral L2CAP disconnect/reconnect TX handling:
- **#76737** "L2CAP TX fragmentation state not cleared upon disconnect" — *priority:
  high, fixed & backported to v3.7-branch* (closest to what we thought #46073 was).
- **#76738** (uninitialized net_buf callback after reconnect), **#43440** (missing
  unref on send failure), **#27434** (TX buffer use-after-free on disconnect).
- Buffer-ownership contract (bt_l2cap doxygen + commit `27b56955d637`): on
  `bt_l2cap_chan_send` success the stack owns the buffer and must invoke the sent
  callback (incl. `-ESHUTDOWN` on teardown). The current `l2cap_chan_del()` drains
  the channel TX queue and unrefs — so channel-queued buffers *should* be reclaimed.
  Our observed non-reclaim on the power-cycle path is therefore either a residual
  path our config hits or state stranded *below* the L2CAP channel; **not** a
  documented standing "never frees" guarantee. State it as observed, not as
  "Zephyr leaks TX buffers."

Corroboration performed via web/GitHub/Nordic search before recording (measured-vs-cited discipline).
