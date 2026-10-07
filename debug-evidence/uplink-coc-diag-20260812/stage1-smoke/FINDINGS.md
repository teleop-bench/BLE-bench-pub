# Uplink CoC — Stage-1 instrumented paired smoke (disposable diagnostic baseline)

Existing startup (UNCHANGED); the new counters decisively localize the reconnect wedge.
2026-08-12 · instrumented `z54-uplink-dk` (blaster) + `z54-uplink-central` (sink), open
Zephyr v4.4.1 + `fsu-m0`. Logs paired by the controller's shared connection **AA**.

## Order B — good order (central up first, peripheral joins) — VALIDATES the instrument
- Blaster: **~153 KiB/s**, `sub≈cmp` (outstanding bounded ~10–11), `pool_wait=0`,
  `fail=0`, 0 mid-run disconnects. (`B-blaster.log`)
- Sink: **~153 KiB/s delivered**, cumulative `total` tracks the blaster's within the
  in-flight window (blaster total 3,540,440 B vs sink 3,489,200 B ≈ 10–11 SDUs skew).
  Same paired AA `0x8ea08dc1`. (`B-sink.log`)
- => end-to-end goodput ≈ source accepted-send; the counters are correct. **Primary
  metric (sink-delivered): ~153 KiB/s.**

## Order A — bad order (peripheral advertising, central connects fresh) — LOCALIZES the wedge
Two connections in one capture (paired AAs):
- **Session 1** (`aa=0xddac7ce8`): blasts ~120 KiB/s, outstanding steady ~11, then
  `pool_wait` starts climbing (allocs failing = pool exhausted), tx→0, and the LINK drops:
  `CoC down: submitted=1711 completed=1700 outstanding=11 pool_wait=79 released=0` →
  `GAP disconnected (0x08)`. `.released` fires immediately AFTER (total released=1).
- **Session 2** (`aa=0xca8cca05`, the reconnect): **`sub=8 cmp=0 out=8`, `pool_wait`
  climbs unbounded, tx stays 0** — the 8 pool buffers are submitted, **none complete**.
  Paired **sink `total=0`** for this session: the 8 SDUs **never reach the sink**.

## What this establishes (evidence-grounded)
- The reconnect wedge is a **TX stall on the reconnected saturated session**: submitted
  SDUs are neither delivered to the sink (`rx total=0`) nor completed back to the app
  (`.sent`/`completed=0`), so the 8-buffer pool exhausts and the sender wedges. It is
  **not** a host `.sent` accounting artifact (nothing was delivered) and **not** simple
  thread blockage (the blast thread runs; `pool_wait` climbs).
- `.released` for session 1 fired BEFORE session 2 connected in this trace, so a naive
  "memset-before-release" ordering is **not sufficient** to explain it on its own —
  something in the reused channel / controller TX state after a mid-saturation `0x08`
  drop does not resume completing.

## Still UNRESOLVED (for Stage 3 to test, not asserted here)
Candidate fixes to compare in reset-isolated, counterbalanced order tests: serialize
`PHY→DLE→CoC` (require confirmed 2M + eff-len 251 before opening CoC); gate channel reuse
on `.released` and remove the premature `l2cap_accept` memset; and a
`CONFIG_BT_CTLR_FORCE_MD_COUNT=0` control (central currently `=10`). Whether the stall is
host-L2CAP (reused static channel), controller TX credits, or the FORCE_MD interaction is
NOT yet determined.
