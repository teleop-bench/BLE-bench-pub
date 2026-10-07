# Deconfound v3 — reboot WITHOUT Order-A: the trigger is the CENTRAL REBOOT (fresh-central lifecycle), NOT ordering

**Result: a rebooted central reconnecting in central-first (Order B) order STILL STALLS the
peripheral uplink — even when given ~5 s to settle before reconnecting.** This isolates the
central **reboot / fresh host+controller lifecycle** as the trigger, **independent of
connection ordering** (and, from `deconfound-v2/`, independent of teardown mode). Per the
reviewer's rubric: *recovery would isolate Order-A timing; stall implicates fresh central
state independently of ordering* → **STALL → fresh central state.**

2026-08-12 · 2× nRF54L15-DK · open Zephyr v4.4.1 + `fsu-m0` · 2M · 7.5 ms · CoC PSM 0x0080 ·
244-B SDUs · periph=blaster=server, central=sink=client · paired by connection AA.

## Method — force a REBOOTED central to reconnect central-first (Order B)
- **Central** (`sink-ctl-scanready-diag.c`, `firmware/central-ctl.*`): mode **g** = graceful
  teardown (LL_TERMINATE 0x13) then `sys_reboot()`. Emits a machine-readable **`SCAN-READY`**
  after every boot's scan start (first boot AND post-reboot).
- **Peripheral** (`blaster-gated-diag.c`, `firmware/periph-gated.*`): auto-advertises on
  **boot** (so session 1 is healthy), but **gates the RE-advertisement after a disconnect** —
  it advertises again only on the host's **`V`** command.
- **Host** (`orch8.py` / `orch8s.py`): session 1 = central-first Order B (healthy, drains).
  After the mode-g teardown+reboot, the periph waits (gated); the host sends `V` only **after**
  the rebooted central's `SCAN-READY`, so the rebooted central reconnects **central-first
  (Order B)**. `orch8s.py` additionally waits **5 s** after `SCAN-READY` so the rebooted
  central is **settled** before reconnecting. New AA / 2M / DLE 251 are logged both ends.

Session 1 in every run is healthy and **fully drained** at teardown (`out=0`, `pool_wait=0`,
`submitted==completed`); ordering is held **constant (Order B)** for session 1 AND session 2;
only the central reboot (and, for the settle variant, the reconnect delay) varies.

## Cells — session 1 healthy Order-B; session 2 = rebooted central, Order B
| run | session-1 drain (AA) | s2 reconnect | central at s2 | **session 2** |
|---|---|---|---|---|
| run1     | `sub=cmp=4928` (aa=0x93265ebf) | Order B, immediate  | rebooted (run 147449612→2303) | **STALL** `sub=8 cmp=0`, sink 0 |
| run2     | `sub=cmp=4879` (aa=0x515b3f3f) | Order B, immediate  | rebooted | **STALL** `sub=8 cmp=0`, sink 0 |
| settle1  | `sub=cmp=4612` (aa=0xef7837a2) | Order B, **+5 s settle** | rebooted | **STALL** `sub=8 cmp=0`, sink 0 |
| settle2  | `sub=cmp=4756` (aa=0xcb4a5f9f) | Order B, **+5 s settle** | rebooted | **STALL** `sub=8 cmp=0`, sink 0 |

All four runs independent (distinct session AAs and byte content — `SHA256SUMS`). Session-2
`SCAN-READY` precedes the periph's `V`/advertise in every run (verified central-first).

## Interpretation — combined with deconfound-v2
| central | teardown | order | settle | session 2 | source |
|---|---|---|---|---|---|
| **reboot** | abrupt 0x08 | A | — | STALL | v2 (a) |
| **reboot** | graceful 0x13 | A | — | STALL | v2 (g) |
| same-boot | graceful 0x13 | B | — | **RECOVER** | v2 (s) |
| **reboot** | graceful 0x13 | **B** | no | STALL | v3 run1/2 |
| **reboot** | graceful 0x13 | **B** | **5 s** | STALL | v3 settle1/2 |

- **reboot vs same-boot** flips the outcome with teardown mode and ordering held graceful/OrderB
  (v2 s = RECOVER vs v3 = STALL) ⇒ **central reboot is the trigger.**
- **Order A vs Order B** does NOT change a rebooted central's outcome (both STALL) ⇒ ordering
  is **not** the mechanism. Order-A (the v2 leading hypothesis) is **refuted** as the cause.
- **Settle time** (0.3 s vs 5 s post-reboot) does not change it ⇒ not a warm-up-timing issue.
- **Teardown mode** (0x08 vs 0x13) irrelevant (v2).

## Leading unified hypothesis (explains all cases, incl. the reboot-free wedge)
**A central's FIRST L2CAP CoC connection after a (re)boot wedges the peripheral uplink;
subsequent same-boot connections stream.** A reboot resets this, so the post-reboot reconnect
(the central's 1st connection since reboot) stalls, while a same-boot reconnect (2nd+ since
boot) recovers. This also explains the reboot-free observations: a peripheral-first ("Order A")
first session stalls because the central is likewise on its 1st-since-boot connection, and the
"healthy Order-B session 1" runs each had a throwaway/stale connection (or pre-establish) first.
Not yet mechanism-root-caused (host L2CAP vs controller per-connection init on the central's
first CoC since boot). Consistent with the earlier nRF52/Zephyr-4.4.1 rig: "central reboot,
not abruptness, triggers the CoC uplink wedge."

## Harness note (interpretation caveat)
An intermediate gated build that withheld the periph's **boot** advertisement (periph booted
then sat idle before advertising) wedged session 1 by itself — a distinct, avoidable artifact.
The archived `blaster-gated-diag.c` auto-advertises on boot and gates only the post-disconnect
re-advertisement, so session 1 is healthy and only the session-2 ordering is controlled.

## Reproduce
`orch8.py <periph_port> <central_port> <outdir> <tag>` (immediate Order-B reconnect) or
`orch8s.py …` (5 s settle) with periph=`firmware/periph-gated.*` (SN 1057719509),
central=`firmware/central-ctl.*` (SN 1057794857), consoles = vcom1 of each DK.
