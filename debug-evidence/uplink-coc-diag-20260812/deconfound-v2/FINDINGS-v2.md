# Deconfound v2 — RETRACTION: the trigger is the CENTRAL REBOOT (Order-A reconnect), NOT the teardown mode

**This supersedes and RETRACTS the `deconfound/` conclusion ("abrupt teardown MODE is the
trigger").** A reviewer noted the v1 abrupt cells *reset the central* (fresh boot) while the
graceful cell reconnected *same-boot* — confounding teardown mode with central lifecycle.
Controlling that with ONE central binary and a runtime-selected teardown shows the earlier
conclusion was wrong: **teardown mode (abrupt 0x08 vs graceful 0x13) does NOT determine the
outcome; the CENTRAL REBOOT does.**

2026-08-12 · 2× nRF54L15-DK · open Zephyr v4.4.1 + `fsu-m0` controller · 2M · 7.5 ms ·
L2CAP CoC PSM 0x0080 · 244-byte SDUs · periph=blaster=server, central=sink=client.

## Method — ONE central binary, runtime-selected teardown (byte-identical reconnect path)
`sink-ctl-diag.c` (build `firmware/central-ctl.*`) reads a mode char from the console and,
16 s after the CoC comes up (while the peripheral sender is fully drained by its producer
-pause), performs exactly one of:
- **a** = abrupt+reboot: `sys_reboot()` immediately (no LL_TERMINATE) → periph sees **0x08**, central FRESH boot.
- **g** = graceful+reboot: `bt_conn_disconnect(0x13)` then `sys_reboot()` → periph sees **0x13**, central FRESH boot.
- **s** = graceful+sameboot: `bt_conn_disconnect(0x13)`, no reboot → periph sees **0x13**, central SAME boot.

The peripheral (`blaster-pause-diag.c`, build `firmware/periph-pause.*`) pauses production at
connect+8 s so the sender is **drained/healthy** (`outstanding=0`, `pool_wait=0`,
`submitted==completed`) at every teardown. A pre-establish + reset order (`orch5.py`) yields
a healthy **Order-B** session 1 (central up first). Every cell below has a healthy+drained
session 1; only the teardown action differs.

## Cells — all with a healthy+drained session 1, n=2 each
| cell | session-1 teardown snapshot (AA) | disc reason | central | **session 2** |
|---|---|---|---|---|
| **a** run1 | `sub=cmp=5081 out=0 poolwait=0` (aa=0x69c58150) | **0x08** | reboot | **STALL** `sub=8 cmp=0`, sink 0 |
| **a** run2 | `sub=cmp=4832 out=0 poolwait=0` (aa=0xb5e59392) | **0x08** | reboot | **STALL** `sub=8 cmp=0`, sink 0 |
| **g** run1 | `sub=cmp=5221 out=0 poolwait=0` (aa=0xbeb258df) | **0x13** | reboot | **STALL** `sub=8 cmp=0`, sink 0 |
| **g** run2 | `sub=cmp=5115 out=0 poolwait=0` (aa=0x1742f7d6) | **0x13** | reboot | **STALL** `sub=8 cmp=0`, sink 0 |
| **s** run1 | `sub=cmp=5095 out=0 poolwait=0` (aa=0xd3188d38) | **0x13** | same-boot | **RECOVER** ~150 KiB/s (sink stream) |
| **s** run2 | `sub=cmp=4903 out=0 poolwait=0` (aa=0x8d33dfa1) | **0x13** | same-boot | **RECOVER** ~150–157 KiB/s |

All six runs are independent (distinct session AAs, distinct byte content — see `SHA256SUMS`).
`poolwait` on a stalled session 2 rises monotonically through the observation window
(e.g. 12→311); it is not bounded.

> **PROVENANCE / correction (2026-08-12):** the first archived `a-run2` and `s-run2` were
> accidental byte-identical **copies** of their run1 (a re-run overwrote the shared scratch
> file before it was copied twice), so the initial evidence was really a=1/1, s=1/1, g=2/2. A
> reviewer caught this. Both were replaced with **freshly collected, reset-isolated** runs
> (the run2 rows above, distinct AAs), restoring genuine **n=2** for every cell.

## Isolation
- **g vs s** — both **graceful (0x13)**, both healthy+drained session 1; differ ONLY in
  central **reboot** (g) vs **same-boot** (s). **g STALLS (2/2), s RECOVERS (2/2)** ⇒
  **the central reboot is the trigger.**
- **a vs g** — both **reboot**, both healthy+drained session 1; differ ONLY in teardown
  **mode** (0x08 vs 0x13). **Both STALL (2/2 each)** ⇒ **teardown mode is irrelevant.**

## Interpretation — what IS isolated: central reboot (fresh-central lifecycle) vs same-boot. Ordering CO-VARIES.
What this experiment isolates cleanly is **central reboot / fresh-central lifecycle** (modes
a, g) **vs same-boot reconnection** (mode s): reboot → stall, same-boot → recover, independent
of teardown mode. It does **NOT** yet isolate *why* the reboot matters, because two things
co-vary with it:
1. **Fresh-central state** — the rebooted central starts from cold host/controller state; and
2. **Order A** — a rebooted central is down during the peripheral's re-advertisement, so it
   rejoins an *already-advertising* peripheral (peripheral-before-central), whereas a same-boot
   central stays up and scanning (central-before-peripheral).

**Order-A is a leading HYPOTHESIS, not established here.** It is suggested by a separate
observation: the **first** session of a boot also stalls whenever the peripheral advertises
before the central (peripheral-first bring-up) — with **no teardown and no reboot at all** —
and streams when the central is up first. (That is why `orch5.py` uses a pre-establish to
force central-first for session 1.) But in the a/g/s cells above, ordering and fresh-central
state are confounded. The **decisive next test (pending)** breaks that tie: **reboot the
central but gate the peripheral's advertising until the rebooted central emits SCAN-READY**,
so a rebooted central reconnects in central-first order. Recovery ⇒ Order-A timing is the
trigger; stall ⇒ fresh-central state independent of ordering.

## What this establishes / does NOT
- **Establishes:** teardown mode (0x08 vs 0x13) does **not** drive the stall; central **reboot
  vs same-boot** does (reboot stalls, same-boot recovers). The v1 "abrupt teardown is the
  trigger" claim is **RETRACTED.**
- **Consistent with** the earlier nRF52/Zephyr-4.4.1 observation (separate rig) that a central
  *reboot* — not abruptness — triggered the CoC uplink wedge.
- **Not yet isolated:** whether the deepest cause is the *reboot* (fresh controller/host state)
  or the *Order-A ordering itself*, independent of reboot. The clean next test is a
  **reboot WITHOUT Order-A** (delay the peripheral's re-advertisement until the rebooted
  central is scanning): if that RECOVERS, the ordering is the cause, not the reboot.
- **Untested:** role reversal (central=server) and the downlink direction.

## Reproduce
`orch5.py <a|g|s> <periph_port> <central_port> <outdir>` with periph=`firmware/periph-pause.*`
(SN 1057719509) and central=`firmware/central-ctl.*` (SN 1057794857), consoles = vcom1 of
each DK. Binaries + resolved `.config` are under `firmware/`.
