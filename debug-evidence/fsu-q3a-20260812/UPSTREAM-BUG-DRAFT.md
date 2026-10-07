# DRAFT — Frame Space Update (FSU) negotiated spacing is reset by a later connection-parameter update

**Status:** draft for upstream discussion. **Scope:** demonstrated on one Zephyr
controller build derived from the open M0 FSU work + the PR-99473 reference import
(`ull_llcp` frame-space); base Zephyr v4.4.1, controller branch `fsu-m0` HEAD
`9999e040446`, board `nrf54l15dk/nrf54l15/cpuapp`, 1 M PHY. **This is NOT a general
Bluetooth conformance claim** — it is a reproducible behavior in this reference-derived
build. Evidence links point ONLY to quarantined diagnostic captures, never to accepted
measurement cells.

## Summary
After a Frame Space Update completes (host receives a successful completion; the radio
adopts the reduced turnaround), a subsequent **connection-parameter update** silently
**resets the effective frame spacing back to the 150 µs default** — with no new FSU
completion signalling that change. A frame space negotiated *before* the update is lost;
one negotiated *after* it persists.

## Minimal reproduction
1. Central connects at a fixed interval (here `BT_LE_CONN_PARAM(6,6,0,400)` = 7.5 ms).
2. Peripheral has the default auto connection-parameter update enabled with a preferred
   interval that differs from the central's (below), so ~5 s after connect it issues an
   L2CAP connection-parameter-update request; the central accepts it.
3. Negotiate an FSU (e.g. 1 M ACL, 150→100 µs) *before* that ~5 s update completes.
4. Observe the achieved turnaround after the update: it is back at 150 µs, although the
   host received an FSU completion at 100 µs and no further FSU/completion occurred.

## Controls — the CONNECTION UPDATE (not the auto-update setting per se) is the discriminator
Three quarantined conditions isolate it. The core A/B is rows 1 vs 3 (identical early-FSU
timing; only the peripheral auto-update differs); row 2 shows the same build with
auto-update ON still persists when the FSU is requested AFTER the update — i.e. it is the
UPDATE that resets the spacing, not the FSU request itself.

| # | condition | on-air RF turnaround (passive observer) | on-chip turnaround histogram | evidence |
|---|---|---|---|---|
| 1 | auto-update **ON**, FSU requested **BEFORE** the ~5 s update | **150.5 µs** (gap_proxy 3047 t) | **150 µs** bin (n=600), no 100 µs bin — **reverted** | `failed-block-b1/abba-1-f100/` |
| 2 | auto-update **ON**, FSU requested **AFTER** the update | (step observed) | **both 150 and 100 µs** bins (100 µs n=535) — **persists** | `diagnostics/early-FSU-dwell3/` |
| 3 | auto-update **OFF**, FSU requested **early** | **100.5 µs** (gap_proxy 2247 t) | **100 µs** bin (n=4005) — **persists** | `diagnostics/autoupdate-OFF-control/` |

RF = a passive nRF52832 raw-radio observer measuring prev-END→next-ADDRESS on 1 M.
on-chip = the peripheral's own RX→TX turnaround histogram. Both instruments agree.
(All three are quarantined diagnostics — never the accepted primary/confirmation result.)

## Exact configuration that triggers it
Peripheral `.config`:
```
CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=y
CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS=y
CONFIG_BT_PERIPHERAL_PREF_MIN_INT=24     # 30 ms
CONFIG_BT_PERIPHERAL_PREF_MAX_INT=40     # 50 ms   (differs from the central's 7.5 ms)
CONFIG_BT_PERIPHERAL_PREF_TIMEOUT=42
CONFIG_BT_CONN_PARAM_UPDATE_TIMEOUT=5000 # request fires ~5 s post-connect
```
Any path that ends in a connection-parameter update (not only this auto-update) exercises
the same reset.

## Source location
`subsys/bluetooth/controller/ll_sw/ull_conn.c`, `ull_conn_update_parameters()`
(function at line ~2326), reached only from `ull_llcp_conn_upd.c` (the connection-update
/ channel-map procedure). On update it unconditionally writes the defaults:
```c
lll->tifs_tx_us  = EVENT_IFS_DEFAULT_US;   /* ~2380 */
lll->tifs_rx_us  = EVENT_IFS_DEFAULT_US;
lll->tifs_hcto_us = EVENT_IFS_DEFAULT_US;
for (size_t i = 0; i < 3; i++) {           /* ~2383 */
    conn->lll.fsu.perphy[i].fsu_min = EVENT_IFS_DEFAULT_US;
    conn->lll.fsu.perphy[i].fsu_max = EVENT_IFS_DEFAULT_US;
    conn->lll.fsu.perphy[i].phys = PHY_1M | PHY_2M | PHY_CODED;
    conn->lll.fsu.perphy[i].spacing_type = T_IFS_ACL_PC | T_IFS_ACL_CP | T_IFS_CIS;
}
```
The negotiated effective FSU state (`conn->lll.fsu.eff`/`perphy`) and `tifs_*_us` are
overwritten with defaults; nothing re-applies the previously negotiated frame space, and
no completion event is emitted to indicate the spacing changed.

## Expected vs actual
- **Expected invariant:** a connection-parameter update should PRESERVE (or RECOMPUTE and
  re-apply) the negotiated effective frame spacing; if it legitimately changes, that
  change should be surfaced (e.g. a new FSU completion), not applied silently. The Core
  Link Layer FSU procedure describes the procedure changing the affected values and the
  controller notifying the Host when values change; it does not describe a connection
  update silently resetting them. (Core LL FSU section:
  https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/low-energy-controller/link-layer-specification.html
  ; feature overview: https://www.bluetooth.com/core-specification-6-feature-overview/ .)
  This is cited as the *expected-behavior rationale* only — see the Scope note; no
  conformance claim is made about this build.
- **Actual:** `tifs_*_us` and the per-PHY FSU state are reset to the 150 µs default on the
  update, with no new FSU completion — so hosts believe 100 µs remains in effect while the
  radio has reverted to 150 µs.

## Proposed fix direction (not validated)
On `ull_conn_update_parameters()`, instead of unconditionally writing
`EVENT_IFS_DEFAULT_US`, preserve the currently negotiated per-PHY FSU state and
re-derive `tifs_tx/rx/hcto_us` from `conn->lll.fsu.eff` after the parameter update
(re-running the same effective-spacing apply used when FSU completes), so an active frame
space survives an interval/latency/timeout or channel-map change. If a reset is ever
intended, it should be paired with a host-visible completion. *(Direction only — no patch
has been written or validated here.)*

## Reproduction tooling
Passive raw-radio observer + runner/analyzer under `q2-central/` (this repo); the A/B
control above was produced with the peripheral built once with and once without
`CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS`, all else identical.
