# Deconfound test — teardown MODE isolated from sender condition (abrupt vs graceful, sender DRAINED in both)

**Result: with the sender held in an IDENTICAL fully-drained/healthy state at teardown
(`outstanding=0`, `pool_wait=0`, `submitted==completed`), an ABRUPT (0x08 supervision
-timeout) reconnect STALLS the next session (2/2), while a GRACEFUL (0x13 LL_TERMINATE)
reconnect RECOVERS to full throughput (1/1). The teardown MODE — not the sender's
saturation/backlog at teardown — is the trigger.** This removes the confound flagged in
the prior `discriminator/` comparison.

2026-08-12 · 2× nRF54L15-DK · open Zephyr v4.4.1 + `fsu-m0` controller · 2M PHY · 7.5 ms ·
L2CAP CoC PSM 0x0080 · 244-byte SDUs · periph=blaster=server, central=sink=client.
Paired by the controller's shared connection **AA**.

## Method — how the confound was removed
A **producer-pause** was added to the blaster (`z54-uplink-dk`, build `/tmp/ul-dk-pause`):
`PAUSE_AFTER_S=8` s after the FIRST CoC comes up, a delayable work stops production
(`producing=false`); the blast loop then idles (`k_msleep(20)`) so the 8-buffer pool
drains and completes. This drives the sender to a **clean, drained, healthy** state
(`outstanding=0`, `pool_wait=0`, `submitted==completed`) BEFORE the teardown is induced.
The pause is armed once (`paused_once`), so session 2 resumes full-rate production. Both
cells use the SAME blaster; only the central's teardown mode differs:
- **abrupt**: the central is **reset** mid-drain (no LL_TERMINATE) → the peripheral sees a
  link-supervision timeout (**0x08**). (`orch.py`, mode `abrupt`/`abrupt2`.)
- **graceful**: the central firmware (`/tmp/ul-cen-gd`, from
  `discriminator/sink-graceful-disc-diag.c` with the self-terminate delay bumped
  `K_SECONDS(10)→K_SECONDS(16)` so it also fires while fully drained) issues
  **LL_TERMINATE** → the peripheral sees **0x13**.

Order **B** bring-up (central up first, then peripheral advertises) is used so **session 1
is healthy** (~155 KiB/s) in every cell — the drain starts from a real streaming state,
not the Order-A first-connection stall.

## Cells (blaster side; sink cross-checked by paired AA)

### abrupt-drained #1 — STALL  (`abrupt-periph.log` / `abrupt-central.log`, AA sess2 `0xe9d794d0`)
- session 1: **155–158 KiB/s** healthy (sink 156–157), `out=11`, `poolwait=0`.
- `12.34 PRODUCER PAUSED … out=11 pool_wait=0` → drained by `14.24`: `out=0 poolwait=0 sub=cmp=5180`.
- `24.24 CoC down: submitted=5180 completed=5180 outstanding=0 pool_wait=0` → **teardown while
  fully drained**; `24.25 GAP disconnected (0x08)`.
- session 2: **`sub=8 cmp=0 out=8`**, `poolwait` 58→457, `total` frozen 1265872; **sink total=0**. STALL.

### abrupt-drained #2 (confirmatory) — STALL  (`abrupt2-periph.log` / `abrupt2-central.log`, AA sess2 `0x329144c6`)
- `12.37 PRODUCER PAUSED … out=11 pool_wait=0`.
- `24.43 CoC down: submitted=4076 completed=4076 outstanding=0 pool_wait=0`; `24.44 GAP disconnected (0x08)`.
- session 2: **`sub=8 cmp=0 out=8`**, `poolwait` 135→434, `total` frozen 996496. STALL.

### graceful-drained — RECOVER  (`graceful-periph.log` / `graceful-central.log`, AA sess2 `0x761026d4`)
- session 1: **~150–155 KiB/s** healthy.
- `12.40 PRODUCER PAUSED … out=11 pool_wait=0` → drained: `out=0 poolwait=0 sub=cmp=5093`.
- `19.57 CoC down: submitted=5093 completed=5093 outstanding=0 pool_wait=0` → **teardown while
  fully drained (identical to the abrupt cells)**; `19.58 GAP disconnected (0x13)`.
- session 2: **155–157 KiB/s**, `sub≈cmp`, `out=11`, `poolwait=0`; **sink total 1242692 → 5148644
  (~3.9 MB delivered)**. RECOVER.

## Comparison (sender condition CONTROLLED)
| cell | sender at teardown | teardown mode | session 2 |
|---|---|---|---|
| abrupt #1 | `out=0 poolwait=0 sub=cmp=5180` | **0x08** supervision timeout | **STALL** (sub=8 cmp=0, sink 0) |
| abrupt #2 | `out=0 poolwait=0 sub=cmp=4076` | **0x08** supervision timeout | **STALL** (sub=8 cmp=0, sink 0) |
| graceful | `out=0 poolwait=0 sub=cmp=5093` | **0x13** LL_TERMINATE | **RECOVER** (~155 KiB/s, sink ~3.9 MB) |

The sender's condition at teardown is **identical** across all three (fully drained,
`out=0`, `poolwait=0`, `submitted==completed`). The only variable is the teardown mode.
Abrupt → stall (2/2); graceful → recover (1/1).

## What this establishes / does NOT establish
- **Establishes:** the reconnect stall is triggered by the **abrupt (supervision-timeout
  0x08) teardown mode**, NOT by the sender being saturated/backlogged at the moment of loss.
  The earlier caveat (abrupt runs had TX already stalled + `pool_wait` climbing before 0x08)
  is **removed** — the stall reproduces with a perfectly clean, drained sender.
- **Does NOT establish the mechanism.** Which state fails to reclaim on an abrupt loss (host
  L2CAP TX-queue/credit vs controller TX context vs new-session init interacting with the
  abrupt-loss episode) is still open. n=2 abrupt / n=1 graceful here (plus prior corpus: 3
  saturated abrupt stalls, 1 healthy graceful recover).
- **Untested here:** role reversal (central=server) and the downlink direction.

## Reproduce
`orch.py <abrupt|graceful> <periph_port> <central_port> <outdir>` with periph =
`/tmp/ul-dk-pause` (blaster+pause) on SN 1057719509 and central = `/tmp/ul-cen-s3`
(Stage-3, for abrupt: reset mid-drain) or `/tmp/ul-cen-gd` (self-terminate at connect+16s,
for graceful) on SN 1057794857. Console = vcom1 of each DK.
