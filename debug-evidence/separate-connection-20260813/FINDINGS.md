# Separate-connection hard-latency-bound mitigation — 2026-08-13 (DIAGNOSTIC-ONLY)

**Goal:** put the stop-signal on its own connection (Periph A) and the bulk on a separate
connection (Periph B), so bulk cannot head-of-line-block the ping — a *hard* safety-RTT
bound under bulk load.

**Rig:** nRF54 central holding two connections — Periph A = nRF54 safety echo
(`safety-peer`, 7.5 ms), Periph B = nRF52832 DK bulk sink (`bulk-peer`, 50 ms). Firmware
+ resolved `.config` for all three boards archived in `firmware/`.

## Defensible observations (corrected per review — clean number NOT obtained)

1. **While second-peer acquisition was active, the safety RTT rose from ~12 ms to ~27 ms**
   (min still 11.9 ms). This is a **correlation** — there are no explicit scan-start/stop
   records or controlled scan-only A/B phases in the archive to prove causation.

2. **One brief window had both peers LL-connected** (`a=1 b=1`), safety RTT ~12.5 ms, 0
   pings >30 ms. **But the second (bulk) link never reached GATT-ready** — `bulk writes=0`
   and no bulk-characteristic discovery record. So this shows a short **LL-connected
   interval, not a functioning concurrent application link**, and (bulk not flowing) does
   **not** demonstrate protection under load.

3. **One bulk `0x08` (supervision timeout) was observed.** Otherwise `b=0` means the bulk
   link was **absent / not ready** — it does **not** localize why, and is **not** evidence
   of "repeated disconnects" or a "two-7.5 ms failure" (those claims are retracted).

## What is NOT established (retractions)

- **The 2M PHY-update was NOT ruled out.** The resolved central `.config` has
  `CONFIG_BT_AUTO_PHY_CENTRAL_2M=y` — the *automatic* central PHY-to-2M procedure is still
  active, so removing the explicit `bt_conn_le_phy_update()` call did **not** disable it.
  "Removing the PHY update didn't fix it" is **unsupported** — retracted.
- **"Open-controller dual-connection instability" is premature.** Supported statement:
  *this diagnostic configuration did not establish a stable, GATT-ready second connection.*
  Cause unlocalized. Paired **endpoint** logs (central + both periphs, time-aligned) are
  also missing.
- **"Scanning doubles RTT"** is downgraded to the correlation in observation 1.

## Cheapest decisive next test (if resumed)

Not broader scheduler debugging — first eliminate the controllable confounds:
- `CONFIG_BT_AUTO_PHY_CENTRAL_NONE=y` (actually disable the auto-PHY procedure) and
  **serialized/controlled DLE**;
- **paired endpoint logs** (central + safety + bulk, time-aligned) with **exact flashed
  firmware/config provenance** (now added in `firmware/`);
- **machine-readable scan/connect/discovery state** transitions on the central (so
  scan-active vs both-GATT-ready phases are unambiguous), and a GATT-ready gate before any
  RTT is attributed.
Only after those are clean does a scheduler-level question become well-posed.

## Practical conclusion (survives)

The **single-connection completion-pacing mitigation** (see
`debug-evidence/latency-under-load-20260813/`) is the **measured** mitigation: it reduced
severe (>30 ms) stop-signal violations from ~99.7 % to ~2.5 % at ~140 KB/s. Describe it as
a **strong empirical mitigation, not a hard latency guarantee.** The separate-connection
*hard bound* is **diagnostic-only / unproven** here.

## Files
- `logs/` — central captures (brief-both-LL-up ~12.5 ms; ~27 ms during acquisition; a
  no-explicit-PHY run that still had auto-PHY-2M active). Diagnostic captures, not a
  completed or paired measurement.
- `firmware/` — HEX + resolved `.config` for central, safety periph, bulk periph.
- `apps/` — 2-connection central source + prj.conf; nRF52832 bulk-sink periph.
