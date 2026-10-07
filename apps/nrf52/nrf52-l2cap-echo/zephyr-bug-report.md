> **Superseded (2026-10-06):** the trigger is not "central reboot" as such. The rebooted central opened the CoC channel
> with 0 initial credits, which trips a Zephyr 4.4 host bug (the acceptor's TX can stall permanently if it queues data
> before credits arrive); same-boot reconnects worked only because leftover credits leaked into the next connection's
> request. The minimal `z44-repro-*` pair "stalling before any reboot" is the same bug (its initiator grants credits
> only after connect). See `debug-evidence/coc-credit-fixes-20261006/README.md`.
> **Reproduced deterministically 2026-10-07** with the minimal pair (acceptor MTU fixed to the spec minimum 23): default
> 6/6 connections stall, control with credits in the request 6/6 healthy (`debug-evidence/zephyr-l2cap-zero-credit-repro-20261007/`).
> **Filed 2026-10-07:** [zephyrproject-rtos/zephyr#121544](https://github.com/zephyrproject-rtos/zephyr/issues/121544).

# Zephyr bug report — L2CAP CoC peripheral TX stalls after central reboot+reconnect

**Status: BLOCKED — DO NOT FILE.** The bug is real and clearly observed in our fuller test
firmware (`z44-uplink-*`), but we could **not** isolate it in a minimal standalone repro
within reasonable effort. Blocker: the minimal `z44-repro-*` peripheral↔central pair **cannot
establish a healthy baseline stream** — it stalls before any reboot is even involved
(`sent=4 done=0` with `seg_recv`+`give_credits(20)`; `sent=5 done=1` with implicit credits).
Since the baseline never streams, it cannot demonstrate the reboot wedge. Attempts made
(2026-07-31): `le_chan` fix, small-payload MTU fix, name filter, ref-count fix, `seg_recv`
credit path — the baseline credit flow still won't run, even though the *identical*
`seg_recv`+`give_credits` pattern streams fine at ~150 KB/s in `z44-uplink-*`. Root cause of
the minimal-repro stall not found; **work paused** rather than chase a bug that would not
reproduce in minimal form. A future attempt should diff the minimal
peripheral/central against the working `z44-uplink-*` firmware config line-by-line to find
why credits don't flow in the minimal case. Until a clean deterministic repro exists, this is
not fileable.

---

## The bug in one paragraph

Over BLE, a **peripheral** streams data to a **central** on an L2CAP CoC (credit-based)
channel. If the **central reboots** and reconnects, both ends report the channel connected
again — but the peripheral's transmit path is **permanently dead**: `bt_l2cap_chan_send()`
keeps returning success, yet the SDUs are never delivered and the `.sent` completion callback
never fires again. Only rebooting the *peripheral* recovers it. A **clean disconnect with the
central staying alive recovers fine** — the wedge is specific to the central *rebooting*.

**Plain-English analogy:** two people on a call, the robot reading numbers to the base station.
If the base station walks out of range and comes back, they resume. If the base station hangs up
and calls back on a *fresh phone*, the call "connects" but the robot's voice no longer reaches
it — the robot keeps talking into a dead line, unaware.

---

## Environment

- Zephyr **v4.4.1** (also expected on nearby 4.x; #76737's fix — the closest prior issue — is
  already present in this version and does **not** resolve this).
- nRF52840 (central) + nRF52832 (peripheral); open **`ll_sw_split`** controller, **no SoftDevice**.
- L2CAP CoC dynamic channel, PSM 0x29. 2M PHY / DLE / 7.5 ms interval (not required to reproduce).

## Steps to reproduce

1. Flash the **peripheral** (accepts a channel + sends a steady stream) and the **central**
   (receives + reboots itself ~10 s after the channel connects).
2. Observe a healthy stream: `sent` and `done` counters climb together.
3. The central reboots on its own and reconnects (GAP + L2CAP both re-establish; the central
   re-grants credits).
4. Observe: on the peripheral, **`sent` keeps climbing but `done` is frozen** — sends are
   accepted but never complete/deliver. State persists until the peripheral is rebooted.

## Expected vs actual

- **Expected:** after reconnect the stream resumes — as it does for a clean disconnect where
  the central stays alive.
- **Actual:** the reconnected channel accepts `bt_l2cap_chan_send()` (returns ≥0) but the sent
  callback never fires and no data reaches the central.

## Key distinguishing detail (put this near the top of the issue)

> A **clean disconnect** with the central staying up recovers fully and repeatedly. The wedge
> reproduces **only when the central reboots** (a fresh stack reconnects to a peripheral that
> never rebooted). Abruptness alone is not the trigger — a *graceful* disconnect followed by a
> central reboot **also** wedges it; a graceful disconnect with the central alive does **not**.

This isolates the fault to the **fresh-central-reconnect path**, not the disconnect/teardown
path — which is where #76737 (fixed) lived.

## Not a duplicate

Searched issues/PRs/discussions; closest prior items, none matching:
- **#76737** — L2CAP TX fragmentation state not cleared on disconnect. *Fixed & backported to
  v3.7; present in 4.4.1, bug persists.* Different failure mode (corrupted fragments, not a
  total completion stall).
- **#76738**, **#43440**, **#27434** — reconnect/buffer-lifetime bugs, all closed; none describe
  a reconnected channel producing zero TX completions.
- **#97056** (merged) — invokes TX callbacks when a channel is *deleted*; scoped to the old
  channel's own queue, not a reconnected channel.

No existing open or closed issue covers "CoC peripheral TX dead after central reboot, no sent
callback." **Novel.**

---

## Minimal repro structure

Two apps, each a **minimal diff** against Zephyr's own in-tree samples
(`samples/bluetooth/l2cap_coc_{acceptor,initiator}`) so the baseline is trusted.

```
repro/
├── peripheral/   # from l2cap_coc_acceptor: + advertising, + send loop, + .sent counter
└── central/      # from l2cap_coc_initiator: + name filter, + receive credits, + self-reboot
```

### peripheral (sender)

```
- advertise connectably as "repro"            [stock acceptor doesn't advertise]
- register L2CAP CoC server, PSM 0x29
- on channel-connected: save chan handle; start send thread
- send thread: loop { alloc buf; add ~20 bytes; chan_send(); sent++ }
- .sent callback: done++            <-- the signal that freezes after the bug
- report thread: every 1s -> printk("sent=%u done=%u", sent, done)
- on GAP disconnect: re-advertise
```

**Correctness details that each cost a debug cycle:**
1. Use **`bt_l2cap_le_chan`**, not the plain `bt_l2cap_chan` the acceptor sample uses — a plain
   chan has no TX endpoint storage; sending returns **-EINVAL (-22)**.
2. Keep the payload **small (~20 B)** so it fits the default CoC MTU (else **-EMSGSIZE (-122)**),
   or set `rx.mtu` on both ends.
3. Track the **`.sent`** completion callback — the `done` counter is the entire diagnostic.

### central (receiver + reboots)

```
- scan; connect ONLY to name "repro"          [else it grabs a random advertiser]
- open the L2CAP channel
- give the channel real receive credits (seg_recv + bt_l2cap_chan_give_credits, ~20)
      ^^^ REQUIRED or the sender stalls at baseline (see below)
- on channel-connected: schedule sys_reboot() in ~10 s   <-- the entire trigger
```

**The trap in the repro itself:** the stock samples are a slow demo (one 17-byte message every
2 s) that never exercises flow control. A naive continuous sender **starves and stalls at ~5
packets even with no reboot** (`sent=5 done=1`), which *masks* the real bug. The central must do
proper credit replenishment (`seg_recv` + `give_credits`), verified to stream healthily with the
reboot disabled, *before* re-enabling the reboot. This is the unfinished piece.

## Expected vs bug log

```
HEALTHY (target):
  sent=40 done=40        both climb together
  >>> central reboots <<<
  sent=95 done=95        resumes cleanly after reconnect

BUG (actual):
  sent=40 done=40        healthy before
  >>> central reboots <<<
  sent=44 done=40        sent climbs, done FROZEN -> accepted but never delivered
  sent=60 done=40        permanently stuck until the peripheral reboots
```

The **frozen `done` after the central reboot** is the reproduction.

---

## Paste-ready issue skeleton

```
Title: L2CAP CoC peripheral TX permanently stalls after the central reboots and reconnects

Zephyr version: v4.4.1
Hardware: nRF52840 + nRF52832, open ll_sw_split controller, no SoftDevice
(If a maintainer can retarget the repro to two QEMU instances / native_sim, note that.)

## Description
On an L2CAP CoC channel, a peripheral streams SDUs to a central. If the central reboots
and reconnects, the channel re-establishes (GAP + L2CAP connected callbacks fire, credits
re-granted) but the peripheral's TX is permanently dead: bt_l2cap_chan_send() returns >=0,
the sent callback never fires, no data is delivered. Only rebooting the peripheral recovers.

## Distinguishing detail
A clean disconnect with the central staying alive recovers fully. The wedge reproduces only
when the central REBOOTS (fresh stack reconnecting to a non-rebooted peripheral). A graceful
disconnect + central reboot also wedges; graceful + central-alive does not. => the fault is in
the fresh-central-reconnect path, not the disconnect/teardown path.

## Steps to reproduce
1. flash peripheral (sender) + central (receiver, self-reboots ~10s after connect)
2. observe healthy stream: sent == done, both climbing
3. central reboots + reconnects on its own
4. observe: sent climbs, done frozen -> TX dead until peripheral reboot

## Expected
Stream resumes after reconnect (as it does for a clean disconnect with the central alive).

## Actual
Reconnected channel accepts sends but never completes/delivers them.

## Not a duplicate
#76737 fixed in this version and bug persists; #76738/#43440/#27434/#97056 checked, none match.

## Minimal reproduction
<link to the two source files / west project>
```

---

## What's left

1. **Central credit flow:** implement `CONFIG_BT_L2CAP_SEG_RECV` + `bt_l2cap_chan_give_credits()`
   on the central so the peripheral streams healthily; confirm `sent≈done` climbing with the
   reboot **disabled** (rules out the baseline stall).
2. **Clean capture:** re-enable the reboot; capture one healthy→reboot→wedge sequence showing
   `done` freezing.
3. **(Nice to have)** retarget to `native_sim`/QEMU or two DKs with instructions a maintainer can
   run without our exact dongles; note if not feasible.
4. Attach the two trimmed source files (peripheral + central) and file.

Source in progress: [`apps/z44/z44-repro-acceptor/`](../../z44/z44-repro-acceptor/), [`apps/z44/z44-repro-initiator/`](../../z44/z44-repro-initiator/). Full
characterization: [`UPLINK-RECONNECT-FINDINGS.md`](UPLINK-RECONNECT-FINDINGS.md) §2.
