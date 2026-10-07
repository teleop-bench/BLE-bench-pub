# Zephyr controller patches — provenance and application

**Base commit (pinned):** zephyrproject-rtos/zephyr
`1f6485eca25431b5ff27ce9a754218c9e559bbbb` (v4.4.1 tree used throughout this
project's nRF54L15 measurements).

## Experimental features in patch 0001

Patch 0001 of the [`fsu-m0-series`](fsu-m0-series/README.md) (and the older loose
`trx-busy-cancel-diagnostic.patch`) also carries two experimental, default-off controller features:
a non-spec peripheral in-event echo (`CONFIG_BT_CTLR_INEVENT_ECHO`) and the
`lll_conn_trx_busy_cancels` cancellation diagnostic counter. The echo defaults to off; no app or
overlay here enables it (the FSU overlays set it `=n` explicitly), and no archived resolved config
has it on.

## What is NOT in any patch

The **52 µs inter-frame spacing is stock Zephyr**, not patched behavior:
`CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y` enables Zephyr's (explicitly
non-spec) low-latency connection mode, and `CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US`
then defaults to 52 (`subsys/bluetooth/controller/Kconfig.ll_sw_split`); stock
`ull_conn.c` writes the value into the per-connection `tifs_{tx,rx,hcto}_us`
fields. The FSU patch series adds the *negotiated* (spec) path to that spacing.

## fsu-m0-series.patch (2026-08-07)

FOUR commits on top of upstream 4.4.1 (`1f6485ec`): (1) the existing
trx-busy-cancel diagnostic baseline, (2) verbatim import of closed upstream
PR #99473 (cvinayak's rebase of kruithofa's FSU contribution — closed STALE,
not rejected; also archived raw as `pr99473-fsu-reference.diff`), (3) M0
defect fixes + HCI bridge (cmd 0x209D, evt 0x35, LE Read All Local
Supported Features 0x2087) + the documented BENCH-ONLY
`BT_CTLR_FSU_BENCH_FORCE_FEAT` shim, (4) the radio-path/adoption fixes found during
bench validation (transitional receive windows; RSP-from-effective-state). NOTE:
the plan calls for the host shim as a SEPARATE patch; in this export it is
still bundled into patch 3 — splitting it out is an open M1-prep cleanup. Spec grounding + known-defect list:
`fsu-m0-cheatsheet.md`. App fragments: `z54-lat-*/fsu-open.conf`
(+ central `m0-f52.conf` sweep step). NOT upstreamable as-is (M1 = real
Extended Feature Set page exchange). Apply: `git am fsu-m0-series.patch`
on 1f6485ec, or use branch `fsu-m0` in the local zephyr tree.

## fsu-m0-tifs-instrument.patch (2026-08-08)

§6.1 physical-timing proxy: 3 commits (instrument rev 1→3) on top of the
FSU-working commit `6a2245e1` (base-commit trailer in the patch + noted here; apply after
`fsu-m0-series.patch`). Reads the controller's shared EVENT_TIMER CC0
(radio_tmr_ready_get) at lll_conn_isr_tx start = RX-PHYEND→TX-READY
interval; rev 3 = exact 1µs histogram, irq_lock snapshot, atomic
post-settle clear, correct pairing, drop accounting. BENCH-ONLY
(BT_CTLR_TIFS_CAPTURE_BENCH). Records/analysis in
debug-evidence/fsu-m0-onair-20260807/ (PREREG-TIMER.md + rev-2/3
amendment; instrument NOT yet validated — positive control pending).
Two clean local git histories are not independently reproducible if the
zephyr checkout disappears; this patch is the durable copy.
