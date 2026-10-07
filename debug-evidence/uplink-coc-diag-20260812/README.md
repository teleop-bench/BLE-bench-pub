# Uplink CoC — one stable 40 s run at ~156 KiB/s; one boot-order-correlated timeout and reconnect wedge — causes UNRESOLVED

**Status: QUARANTINED DIAGNOSTIC.** This archives a raw observation only. The
earlier "two-defect" causal narrative (a PHY-collision root cause + a CoC credit
underflow) is **RETRACTED** — see "What is NOT established." Do not cite this as a
delivered end-to-end throughput result, and do not use this benchmark path for FSU
characterization until it is stabilized and qualified.

2026-08-12 · 2× nRF54L15-DK · open Zephyr v4.4.1 + `fsu-m0` controller (HEAD 9999e040) ·
2M PHY · 7.5 ms · L2CAP CoC PSM 0x0080 · 244-byte SDUs · apps `z54-uplink-central`
(central = sink = L2CAP client) + `z54-uplink-dk` (peripheral = blaster = server).

## Observation (facts)
- **Order B (central up first, peripheral joins):** 40 s, **0 disconnects**, sustained
  **~156 KiB/s** — 38 samples after excluding the first partial window: median 156,
  mean 155, range 144–158 KiB/s. (`logs/ul-orderB-40s.log`)
- **Order A (peripheral advertising, central connects fresh):** blasts ~155 KiB/s
  briefly, then **one** stall → `GAP disconnected (0x08)` (link supervision timeout
  expired), **one** reconnect that sends **exactly 8 SDUs = 1952 B** and then wedges.
  (`logs/ul-periph-orderA.log`, `logs/ul-central-full.log`)

## Unit + metric caveat (important)
The throughput is **sender-side accepted-send** throughput: `tx_bytes` counts bytes
accepted by `bt_l2cap_chan_send` (`z54-uplink-dk/src/main.c:240,247`), NOT sink-delivered
goodput. The firmware prints "KB/s" but divides by 1024, so the unit is **KiB/s**. A
**simultaneous sink log was not captured** — this is not yet an end-to-end goodput number.

## What is NOT established (retractions)
1. **`inflight=-1956` is an APP INSTRUMENTATION bug, not a CoC underflow.** `inflight` is
   decremented in `chan_sent` (`:60-63`) but **never incremented** on the send path
   (`:236-248`), so it just counts completion callbacks negatively. Do NOT clamp it;
   replace with correct monotonic submitted/completed counters. (`.sent` = controller
   completion, whose exact timing is implementation-dependent per the Zephyr L2CAP API.)
2. **The PHY rejection is observed but its causality is UNPROVEN.** The central logs
   `LE Set PHY → 0x0c` (`bt_conn: Failed LE Set PHY (-13)`), yet immediately afterward
   BOTH ends report 2M and the link reaches full rate. The logs do NOT show this rejected
   command causing the 0x08 timeout ~6 s later. This is a **setup-procedure race
   CANDIDATE**, not a defect.
3. **"Cyclically reconnects" is RETRACTED.** The Order-A log shows ONE drop and ONE
   wedged reconnect, not repeated cycles.
4. `0x08` confirms the **link supervision timeout expired**, but not its **root cause**.

## Strongest clue (candidate to investigate — not proven)
The reconnect sends exactly **8 SDUs = the 8-buffer `tx_pool`**, then `net_buf_alloc(&tx_pool,
K_FOREVER)` (`:237`) blocks forever → **pool exhaustion / buffers not reclaimed**. This
could be caused or amplified by the app itself:
- `net_buf_alloc(..., K_FOREVER)` can strand the blast thread (`:237`).
- `tx_win` is reset+replenished in `chan_connected` (`:74-77`) but **never consumed** (the
  "SIMPLE blast" loop bypasses it).
- `blocked` is reset (`:79`) but **never incremented** — the wedge signal is dead.
- `le_chan` is `memset()` on accept, potentially before the previous channel's full
  release; there is **no `.released` callback** gating channel reuse.
- **`CONFIG_BT_CTLR_FORCE_MD_COUNT` is asymmetric** (central `=10`, periph `=0`) — a custom
  controller-behavior **confound** for stability.

This is a **regression test motivated by** Zephyr #46073
(https://github.com/zephyrproject-rtos/zephyr/issues/46073) — NOT proven equivalent to it
(#46073 was IPSP, missing credits/TX contexts, a different Zephyr generation).

## Recommended next steps (to qualify this path before any FSU use)
1. Repair instrumentation: monotonic `submitted`/`send_failed`/`.sent` counters
   (increment BEFORE `bt_l2cap_chan_send`, roll back on failure); timed/non-blocking pool
   alloc + pool-wait counts; atomic byte-counter exchange on BOTH sender and receiver;
   session/run ID + AA + boot tag + paired endpoint logs; wait for `.released` before
   reusing the channel object.
2. Serialize `PHY → DLE → CoC` startup and compare vs the current startup in
   reset-isolated, counterbalanced runs; require confirmed 2M and effective TX/RX length
   251 BEFORE opening the CoC.
3. Add a `CONFIG_BT_CTLR_FORCE_MD_COUNT=0` control (remove the FORCE_MD confound).
4. Only THEN run the offered-load ramp, reporting sink-delivered goodput, sender
   completions, pool pressure, disconnects, and reconnect recovery separately.

## Spec references
- Zephyr L2CAP chan ops (`.sent` semantics): https://docs.zephyrproject.org/latest/doxygen/html/structbt__l2cap__chan__ops.html
- Controller error codes (0x08 = Connection Timeout): https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/architecture%2C-change-history%2C-and-conventions/controller-error-codes.html

## Also archived (today's other captures — kept separate, NOT part of this issue)
- `logs/lat-baseline-central.log` — RTT latency baseline (7.5 ms, clean).
- `logs/tput-downlink-sink.log` — downlink CoC ~150 KB/s (sink-side rx).
