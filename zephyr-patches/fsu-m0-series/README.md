# FSU-M0 controller patch series (reproducible export)

> **Prefer the ready-made fork for building.** These patches are also published, already applied,
> as the **`fsu-m0` branch of [github.com/teleop-bench/zephyr](https://github.com/teleop-bench/zephyr/tree/fsu-m0)**
> (fork of upstream, base **v4.4.1**, HEAD `8f44a3d7a3f`):
> `west init -m https://github.com/teleop-bench/zephyr --mr fsu-m0 zephyrproject && cd zephyrproject && west update`.
> The `git am` route below is the equivalent from-scratch / audit path (useful for reviewing the diffs).
> The same 16 commits rebased onto **v4.4.2** are on branch **`fsu-m0-v442`** (HEAD
> `fee9fbc620959b218cda34602d7653d21f7f6a51`) — the tree the September clean GATT/FSU campaign was
> measured on. These patch files reproduce the **v4.4.1** line only.

## Credit

Patch **0002** imports, as-is, the FSU controller implementation from upstream Zephyr
[PR #99473](https://github.com/zephyrproject-rtos/zephyr/pull/99473) by **Vinayak Kariappa
Chettimada**, itself a rebase of [PR #82324](https://github.com/zephyrproject-rtos/zephyr/pull/82324)
by **Andries Kruithof** (both Nordic Semiconductor). Their `Signed-off-by` lines from the upstream
commit are carried in that patch as `Co-developed-by`/`Signed-off-by` trailers. Patches 0003–0016
build on their work; the FSU host API used by the apps is upstream
[PR #93783](https://github.com/zephyrproject-rtos/zephyr/pull/93783) (merged).

Exported controller changes that make up the **candidate Q3 firmware base** for the
BLE 6.0 Frame Space Update (FSU) on-air verification. This is the pinned, reproducible
form of the Zephyr `fsu-m0` branch so the controller is rebuildable from an upstream
tag + these patches. **UPDATE: Q3 is DONE — on-air application of FSU is formally accepted by the
nRF52-observer protocol** at 1M (150→100 µs, `debug-evidence/fsu-q3a-20260812/`) and 2M (150→52 µs,
`debug-evidence/q3-2m-accept-20260906/`), with on-chip cross-validation. This is observer-based, not
professional-analyzer qualification. (The earlier `q3-2m-smoke`/`q3-2m-goodput` dirs are quarantined
smoke runs, superseded by the acceptance.) The earlier "pending Q3 / not-an-assumption" phrasing
below predates that and is retained only as the historical rationale.

## Base

| | value |
|---|---|
| Upstream tag | **`v4.4.1`** (annotated tag object `247e755247840abefa16136168b4a56682150f50`) |
| Tag → commit | **`1f6485eca25431b5ff27ce9a754218c9e559bbbb`** (`v4.4.1^{commit}`; the patch base) |
| Series length | 16 patches (`v4.4.1..fsu-m0`) |
| `fsu-m0` HEAD | `8f44a3d7a3f` |
| Post-apply tree | **`c1b1f41d1c826daf25c5fa86a87409c4d575e713`** |

## Clean-apply check (verified)

```
git worktree add --detach <tmp> v4.4.1
cd <tmp> && git am --no-3way zephyr-patches/fsu-m0-series/*.patch   # all 16 apply, exit 0
git rev-parse HEAD^{tree}                                           # == c1b1f41d1c82...
```

`git am` of all sixteen patches onto a fresh `v4.4.1` reproduces tree
`c1b1f41d1c826daf25c5fa86a87409c4d575e713`, **byte-identical** to `fsu-m0` HEAD
(`git diff` empty). No `-3way`/fuzz needed. (Patch **0016** is the host-side
`l2cap_pull_pdus` counter — see layer D below; needed only for the Stage-2 TX-staging
host-handed measurement, gated on `CONFIG_BT_TESTING`, no-op otherwise.)

## Four layers (patch-level separation)

The categories are cleanly separated **by commit**; some files (e.g. `lll_conn.c`,
`Kconfig.ll_sw_split`) are touched by more than one layer, but never in the same patch.

### A. FSU implementation — the BLE 6.0 feature (LL_FRAME_SPACE_REQ/RSP 0x3B/0x3C, HCI 0x209D/evt 0x35, feature bit 65)
- `0002-FSU-reference-import-PR-99473-*` — PR #99473 imported as-is (cvinayak rebase of kruithofa #82324): opcodes, LLCP common-procedure integration, `pdu.h`, `ll_feat.h`, `ull_llcp*`.
- `0003-M0-FSU-defect-fixes-HCI-bridge-*` — defect fixes on the reference + the HCI bridge (`hci.c`).
- `0004-M0-FSU-WORKING-*` — transitional receive windows + RSP-from-effective-state: implements the intended effective-state behaviour so the negotiated spacing applies. **On-air application is now formally accepted (1M `debug-evidence/fsu-q3a-20260812/`, 2M `debug-evidence/q3-2m-accept-20260906/`; observer + on-chip).**
- `0010-M0-FSU-responder-Host-notification-fix-*` — responder `frame_space_updated` fix (smoke-1): preserve the RX-time `ull_fsu_update_eff` changed flag into `ctx->data.fsu.ntf_fsu` + add the `PROC_FRAME_SPACE` branch to `rp_comm_ntf` (was DLE-only). The initiator side already notified; this delivers the peer-Host callback. Adoption itself was unaffected (the on-air tIFS already reduced).

### B. Bench-only instrumentation — on-chip tIFS histogram (`CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH`)
- `0005-M0-6.1-instrument-EVENT_TIMER-*` — RX-PHYEND→TX-READY EVENT_TIMER CC0 capture (baseline).
- `0006-*-rev-2-*` — pairing fix (latch programmed tIFS → attach to following CC0) + bounded ISR histograms with drop accounting.
- `0007-*-rev-3-*` — exact 1 µs CC0 histogram (real preregistered median), locked drain, atomic clear for post-settle isolation, skip-first-after-connect, `drop=0` required.
- `0008-*-rev-4-*` — freeze-then-snapshot (memset outside `irq_lock`; drain reads frozen bins lock-free — safe on the 1 ms radio path).
- `0014-M0-6.1-instrument-rev-8-*` — capture the response READY into the DEDICATED sample CC (CC3), leaving the controller-owned CC0/TRX untouched (fixes the rev-7 ~28% peripheral-RX perturbation). New bench-only DPPI HAL helpers (radio.c) + BUILD_ASSERTs (ISR-profiling off, no FEM).
- `0013-M0-6.1-instrument-rev-7-*` — RE-ARM the RADIO EVENTS_READY->CC0 capture for the peripheral response TX (disabled by the RX status reset), so CC0 reflects the response-TX READY not the stale event-start RX READY. New bench-only HAL helper (radio.c).
- `0012-M0-6.1-instrument-rev-6-*` — PENDING-SAMPLE LATCH: fixes the peripheral under-sampling (latch tifs at switch-program, arm on CRC-good RX, consume once at isr_tx OR isr_done, labelled with the latched tifs). Replaces `tifs_prog_prev`.
- `0011-M0-6.1-instrument-rev-5-*` — TX-hook DIAGNOSTIC counters (smoke-1 showed n=1): `TIFSDIAG calls=.. fresh=.. drop=.. role=..` (no timing-source or hook-placement change, but adds counter writes in the radio ISR = minor instrumentation with possible perturbation; analyzer ignores it) to reconcile capture attempts vs response opportunities and locate the n=1 cause (gating vs wrong role-path) before any TX-hook relocation.
- Emits `TIFSBIN tifs=.. phy=.. n=.. nv=.. min=.. med=.. max=.. drop=..` (exact-µs bins); consumed by `q3_onchip_check` as the on-chip cross-validation of the on-air step.

### C. Diagnostics — Q2 ground-truth measurement (bench/diagnostic only; not shipped behaviour)
- `0001-bench-baseline-trx-busy-cancel-diagnostic-*` — the trx-busy-cancel baseline the FSU work was built on. **Caveat:** this patch ALSO carries a substantial non-spec `BT_CTLR_INEVENT_ECHO` implementation (Kconfig + guarded hot-path code), not merely the trx-busy diagnostic. It is `n` by default; **Q3 builds MUST explicitly disable it** (`CONFIG_BT_CTLR_INEVENT_ECHO=n`, present in every FSU overlay and hard-asserted by `assert_fsu_config.py`). The historical patch is kept unchanged; only its classification is corrected here.
- `0009-M0-Q2-diagnostic-per-channel-*` — controller-owned per-channel denominators (`lll_conn_q2_evt/tx/curchan`), connection identity (`lll_conn_q2_session/aa`), and the `Q2CONN AA=.. CRCINIT=.. map=..` print, so a passive raw-radio observer gets **non-circular** retention denominators + connection tuning. **No intended protocol-behaviour change, but it is NOT data-path-free**: it adds volatile writes + counter increments in the prepare / RX / TX hot paths (`lll_central.c` prepare, `lll_conn_isr_rx`, `lll_conn_isr_tx`), i.e. hot-path instrumentation with possible timing perturbation.

### D. Bench-only host counter — TX-staging (2026-08-26)
- `0016-bench-gated-host-handed-PDU-counter-l2cap_pull_pdus-*` — a single `volatile uint32_t
  l2cap_pull_pdus` in `subsys/bluetooth/host/l2cap.c`, bumped once per non-NULL `l2cap_data_pull`
  return = host→controller ACL fragments actually handed down. Compared to the controller-aired
  `trx_cnt` it proves the downlink event is airtime-full (host hands **1:1** with what airs), not
  staging-throttled — the evidence that resolved the "refill wall" as tIFS airtime
  (`coc-technical-overview.md §11.1`, `tx-staging-investigation.md`). **Gated on `CONFIG_BT_TESTING`
  → compiles out and is a complete no-op for every FSU/throughput build.** Host-side, so no radio
  hot-path perturbation. Needed only to rebuild the Stage-2 `*_BTTESTING_*` hexes.

## Historical-patch overlap

Two loose patch files under `zephyr-patches/` predate this series and cover the **same
intent** as commits here (now the authoritative form):
- `zephyr-patches/trx-busy-cancel-diagnostic.patch` ↔ patch **0001**.
- `zephyr-patches/q2-print-conn-params.patch` ↔ patch **0009** (both commit messages cite it).

Prefer this series; the loose patches are kept only as historical provenance.

## SHA-256 — patches

```
56b262d99ef6f83e32a2c7e2ba240e3e102530a7d9852c7ae13f934706fee1b0  0001-bench-baseline-trx-busy-cancel-diagnostic-zephyr-pat.patch
0456ed17ae99c48de1ba1e2d0a07d5378a9129d2e7227fbac1819150f097e8bd  0002-FSU-reference-import-PR-99473-as-is-cvinayak-rebase-.patch
ea3d28a54f3e6684ebe3ac9ecf2e845fbef8ab5257c52f1bf8aee67082b8b0df  0003-M0-FSU-defect-fixes-HCI-bridge-on-top-of-PR-99473-re.patch
48c3435a2f34b283fbfd7d9c7a4469afc1fc5c75182ec7b02c801bcec4050906  0004-M0-FSU-WORKING-transitional-receive-windows-RSP-from.patch
e75af3a54d1a7c55615c79761448c2e9af2859712c000ade3203dcbf973d4438  0005-M0-6.1-instrument-EVENT_TIMER-RX-PHYEND-TX-READY-cap.patch
40808ba7561eb71541104cca22bd7d62bfa67f4dc2eee59c6462e0217076f86f  0006-M0-6.1-instrument-rev-2-fix-pairing-latch-programmed.patch
68e6d6df7e78780e63a808684dda1890e10493fe2dc7ab0f96ebcb4b38a10316  0007-M0-6.1-instrument-rev-3-exact-1us-CC0-histogram-real.patch
ad4122cdcc73dc298ac9b9352873f6c9c24809659a7c2d6c05444efa370bf21b  0008-M0-6.1-instrument-rev-4-freeze-then-snapshot-memset-.patch
fbfcc4383d331cc3ea7845684688d672685a8c2a19553a8a6460c917686e16c5  0009-M0-Q2-diagnostic-per-channel-ground-truth-counters-Q.patch
171ad6206e38b44f468e89e7b14d1783c7ef86edf6eb0fc371bf7796bc6a7e00  0010-M0-FSU-responder-Host-notification-fix-frame_space_u.patch
0838476657da26ecb19750e4f4a75c3e527bb91a97d9a3f58ade4ce1d62edb28  0011-M0-6.1-instrument-rev-5-TX-hook-diagnostic-counters-.patch
094ded6fad6cb7904d771ee834ff58c9295d9cf5aee9ac3d0b95db56f70255f0  0012-M0-6.1-instrument-rev-6-pending-sample-latch-fix-per.patch
9f737309b5dcaffb16f3b4aae0619e2d6b9ea7aaee344569bb5b616a87e06db7  0013-M0-6.1-instrument-rev-7-re-arm-the-READY-capture-for.patch
7fef58b633883fd9599216a5f38148660869261c0c43698e0b140816428bdcae  0014-M0-6.1-instrument-rev-8-capture-response-READY-into-.patch
96efc9d8502ef5eb08d228729206bf9f64cdd8a190fbd40c4fa558c2e31f6d4e  0015-M0-FSU-event-fill-diagnostic-Kconfig-gate-BT_CTLR_FS.patch
b750832f2977c6b4502fee8d7e0fc7e11f508327d598f69e4fcc42f4e10eb306  0016-bench-gated-host-handed-PDU-counter-l2cap_pull_pdus-.patch
```

## SHA-256 — final source files (at `fsu-m0` HEAD / post-apply tree)

```
a51f921c1ba42093b905539221e0a1dd7fc6ed97be9af90bfaa227b36a8f8662  subsys/bluetooth/controller/Kconfig.ll_sw_split
e429d450f3025f0f058ae82b193e82106b9cc6b3fabb13fd5fabb7a1bed66d95  subsys/bluetooth/controller/hci/hci.c
fdd20d0a09aa84ffe0dbc3c5e126921577ff63d95b4ddbcbc3122e0ccdfc44ab  subsys/bluetooth/controller/include/ll_feat.h
9e65ed1ce64315cdff90cc456cb9b2e54820946af9f0cc128a7099dbdcd0f003  subsys/bluetooth/controller/ll_sw/lll_conn.h
07ffa0559fa469e6da489389b4da056f96587bd62fd8210713a1682bee1d52f8  subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/radio/radio.c
766a3c32bff9924db2896a8f87d122ec2695ad400f983e7869dabdf15b610c4d  subsys/bluetooth/controller/ll_sw/nordic/lll/lll_central.c
569d6bc6c297f0c61eec9d3fd5cd09a70395c5d9c7ca0ffa139fafd65105c508  subsys/bluetooth/controller/ll_sw/nordic/lll/lll_conn.c
246e5afe4a9d75b6eac3cb609fc849d80df90ef62a053d8edf12208e5c7ad6e9  subsys/bluetooth/controller/ll_sw/nordic/lll/lll_peripheral.c
1a1038b20a59d1a9eeb545567a4a0b236d03e9e6687dcada173dd435d5175f2c  subsys/bluetooth/controller/ll_sw/pdu.h
1fbd3a9d051ff2a9263be7002c53423334459925f27f2d7703a8ba543d4dbc54  subsys/bluetooth/controller/ll_sw/ull_central.c
96998404b483f90a4d9986f3b7e31251ace68fc63fc046a6b8c70644fc5deba8  subsys/bluetooth/controller/ll_sw/ull_conn.c
e1eb89d091831fc57bd602f85a51ac7f95f9983d025d9c4459f2bafa62247a84  subsys/bluetooth/controller/ll_sw/ull_conn_internal.h
b349afdef67c72a5aa294a63b1868323b2567919caf790cebeb0eab55793275a  subsys/bluetooth/controller/ll_sw/ull_llcp.c
0ef5c34abd856d653f1b796f0f20ce545da018a0c4a0b676e7f720c85cfee969  subsys/bluetooth/controller/ll_sw/ull_llcp.h
f63feb0f96fc5e029a9c2d1303277bd9041cfcefb92fa82417a536a7e78ea87f  subsys/bluetooth/controller/ll_sw/ull_llcp_common.c
a6d9687257f16fadbbecdb0036777364b214917bbaa8ff4e1233cc643dc9a0cf  subsys/bluetooth/controller/ll_sw/ull_llcp_features.h
d3c6e2046fdef496f020dd03e8f57febe3f8bba4735e2ca313e3c5f72721917a  subsys/bluetooth/controller/ll_sw/ull_llcp_internal.h
a4482460fbff0a4806fd1b75d3206608623c982fc21988a3348198600ce4769f  subsys/bluetooth/controller/ll_sw/ull_llcp_local.c
d4d0f5daa343a515f3e694573a23938bbd2d75cc3f98e19225a1a615d32115ba  subsys/bluetooth/controller/ll_sw/ull_llcp_pdu.c
69b18583adf095e4baced09ca27e0de66d3f05128fc89ed273bd7b20b76a5da0  subsys/bluetooth/controller/ll_sw/ull_llcp_phy.c
9a3c7846c20c926d5be9c9551e6162d6768539a12f8452fc1fb8a58e50839381  subsys/bluetooth/controller/ll_sw/ull_llcp_remote.c
d06d04890580b7922dcc961db0cdaccaa822d3eb7c702fc12bdbae683117666b  subsys/bluetooth/controller/ll_sw/ull_peripheral.c
```

## The 52 µs floor (was step 7) — root-caused and fixed

The FSU overlays already requested `CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y` and
`CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US=52`, but both were **silently dropped**: they
live inside the `menu "Advanced features"` (`Kconfig.ll_sw_split`, `visible if
BT_CTLR_ADVANCED_FEATURES`), which was unset — so the build was clean yet kept the
150 µs floor. The fix is **`CONFIG_BT_CTLR_ADVANCED_FEATURES=y`** (NOT another attempt
to set `CONN_INTERVAL_LOW_LATENCY`), now added to every FSU overlay along with an
explicit `CONFIG_BT_CTLR_INEVENT_ECHO=n`.

`assert_fsu_config.py <.config> --arm {f100|f150|periph}` hard-gates the resolved
config: advanced features + low-latency interval + **tIFS = 52** + host/controller
FSU + FSU feature set + `FSU_BENCH_FORCE_FEAT` + 1M-only (the 2M acceptance uses the companion
`assert_fsu_config_2m.py`, which instead requires `PHY_2M` + `USER_PHY_UPDATE`); the registered request
params (f100 = 100/150, f150 = 150/0) + Q3 mode + `BT_CENTRAL` on central arms;
`BT_PERIPHERAL` + on-chip bench on the peripheral (bench off on central); in-event
echo off. It FAILS loudly on the old 150 µs config,
a wrong-arm central build, or a missing `.config`. **`q2_run.py` enforces it**: every
`--q3` run validates BOTH endpoint `.config` files before ANY flashing (observer + endpoints) and QUARANTINES
(`q3-fsu-config-invalid`) on failure, recording the result + script hash in the manifest.

## Rebuilt provenance (after applying the series + the overlay fix)

Corrected pristine builds (both PASS the assertion; tIFS floor configured/resolved to
52 µs; the physical effect was later accepted on air — see the top of this file). Resolved
`.config` + `build_info.yml` are archived textually under `provenance/`; full command,
toolchain (Zephyr SDK 1.0.1), and hashes are in `provenance/PROVENANCE.md`.

| arm | app + overlay | `.config` sha256 | assertion |
|---|---|---|---|
| f100 | `q2-central` + `fsu-f100.conf` | `59ad238bfe5a…` | PASS (100→150; bench off) |
| f150 | `q2-central` + `fsu-f150.conf` | `d3d4e44892…` | PASS (150→0 control; bench off) |
| periph | `q2-periph` + `fsu.conf` | `7fb69b69dfb7…` | PASS (responder; bench on) |

> HEX/ELF hashes are recorded in `provenance/PROVENANCE.md` but are this-host only
> (west artifacts are not bit-reproducible across environments) — re-pin at smoke
> (step 8). The earlier hash-only provenance (`.config` `9db55a34…`) was built BEFORE
> the `ADVANCED_FEATURES` fix (still 150 µs) and is superseded.
