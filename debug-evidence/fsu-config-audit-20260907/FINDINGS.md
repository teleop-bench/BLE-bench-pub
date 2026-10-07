# FSU / config-correctness audit — 2026-09-07

**Trigger:** the CoC sink FSU config (`apps/coc/coc-sink/open-fsu.conf`) was found missing
`CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY` → FSU silently negotiated `spacing=150` (no reduction,
~0% delta, a false "FSU does nothing"). Before sharing the benchmark externally, audit *everywhere
else* the FSU / interval / floor configs could be wrong or lapsed. Discipline: **verify achieved
`spacing=52` on hardware, never infer it; reuse proven configs; don't lose ephemeral builds.**

## The FSU "floor" — all 4 required on BOTH link ends (open controller)
- **F1** `CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US=52`
- **F2** `CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y`
- **F3** `CONFIG_BT_CTLR_ADVANCED_FEATURES=y`  (F1/F2 live in a menu `visible if` this — else silently dropped)
- **F4** `CONFIG_BT_CTLR_FRAME_SPACE_UPDATE=y`

SDC configs use a *different* mechanism (`SDC_ENABLE_LOWEST_FRAME_SPACE`) — the 4 open symbols don't apply.

## Hardware-verified engagement (this audit) — `spacing=52`, from-boot capture
| Transport | Firmware | Achieved | Throughput | Evidence |
|---|---|---|---|---|
| CoC one-way open FSU | v4.4.1 prebuilt `c-coc-open-fsu-15` + `p-coc-sink-open` | **`fsu=52 spacing=52`** (`Q3FSU-DONE`) | 154.7 KB/s | `captures/v441fsu-cen.log` |
| CoC one-way open FSU | v4.4.2 rebuilt (sink fix `a27ed8a`) | `spacing=52` | +8.7% @15ms | v442-benchmark-20260907 (earlier) |
| GATT one-way open FSU | v4.4.1 prebuilt `c-gatt-open-fsu-7p5` + `p-gatt-sink-open` | **`FSU: updated status=0x00 spacing=52 us`** | ~187 KB/s | `captures/gatt-open-fsu-boot-cen.log` |
| CoC duplex open FSU | rebuilt v4.4.2 (sink WITH `open-fsu.conf`) | **`Q3FSU-DONE spacing=52`** | ~78-92 KB/s/dir | `captures/duplex-open-fsu-boot-cen.log`; firmware in `duplex-fixed-firmware/` |

| CoC latency-under-load (coclat) | rebuilt (sink WITH `open-fsu.conf`) | **`Q3FSU-DONE spacing=52`, sink `fsu=52`** | — | `captures/coclat-open-fsu-boot-cen.log`; `coclat-fixed-firmware/` |
| CoC 2-channel lane (coclat2) | rebuilt (sink WITH `open-fsu.conf`) | **`Q3FSU-DONE spacing=52`, sink `fsu=52`** | — | `captures/coclat2-open-fsu-boot-cen.log` |

**All FSU transports HW-confirmed `spacing=52` from boot (CoC, GATT, duplex, coclat, coclat2).** The
config files are correct; the failures were in the *recipes* (duplex + both coclat sinks) and in
*labels/claims* (below), not the overlays.

## Config-correctness verdict (static sweep, cross-checked to proven configs)
- **CoC (self-contained overlays):** ALL COMPLETE after the sink fix. No sibling reproduces the bug.
  central/sink/duplex/coclat/coclat2 open-fsu + open-nofsu all carry F1–F4.
- **q2 / on-air observer configs:** all complete (F1–F4).
- **z54-lat + eatt (`fsu-m0` stacked model):** floor lives in `fsu-open.conf` (has F1–F4 + bench shim);
  the `m0-f*` / `hh-fsu52` fragments are **MIN-only** and correct *only when stacked with* `fsu-open.conf`.
- **GATT FSU source config EXISTS** (resolves a false alarm): the GATT *throughput* benchmark is built
  from `apps/nrf54l15/z54-lat-central` (the notify-blast app), whose `fsu-open.conf`+`hh-fsu52.conf`
  carry the floor — REPRODUCE.md:148. HW-verified `spacing=52` above. (A parallel search of the
  `z54-gatt-*` apps wrongly concluded "missing" — those aren't the apps behind the headline GATT result.)

## Defects to FIX before an external share
1. **Duplex recipe builds a floor-less sink.** REPRODUCE.md:205 `west build … coc-duplex-sink` adds no
   `-DEXTRA_CONF_FILE`; `coc-duplex-sink/prj.conf` carries no floor → documented recipe yields a
   silent-150 sink (same class as the CoC-sink bug, but in the *recipe*). FIX: add `open-fsu.conf`.
   (The one-way sink recipe at :172 *does* document the overlay — duplex is inconsistent.) [verifying on HW]
2. **GATT-SDC hex label vs recipe interval.** Hexes named/labeled `@25 ms` (`prebuilt-hexes/README.md:12`,
   `run-all.sh:59-60`) but the only build recipe hardcodes `CONFIG_APP_CONN_INT_UNITS=6` = 7.5 ms
   (REPRODUCE.md:151-152). Either relabel the hexes 7.5 ms or add a units=20 recipe.
3. **"Only the granted gap differs" isolation claim** (REPRODUCE.md:176-178) is TRUE for the CoC arms
   (open-fsu vs open-nofsu differ only in `APP_FSU_MIN/MAX_US` 52→150) but FALSE for the GATT arms: the
   GATT FSU-off arm drops `fsu-open.conf`+`hh-fsu52.conf` entirely, removing F1–F4 + EXTENDED_FEAT_SET —
   many variables change, not just the gap. FIX the wording (scope the clean-isolation claim to CoC).
4. **`BENCH_FORCE_FEAT` disclosure.** Both CoC and GATT "FSU on" arms set
   `CONFIG_BT_CTLR_FSU_BENCH_FORCE_FEAT=y` = "forces FSU feature known-supported (no real page exchange)."
   FSU capability is *forced*, not air-negotiated. Documented in-config but a credibility-sensitive reader
   needs it stated in the overview/README. (The *spacing reduction itself* is real & on-air-observed via
   the nRF52 sniffer + 1M/2M formal accept — the shim only skips the feature-bit exchange.)

## Latent footguns (not committed bugs, but one flag-slip from the silent-150 failure)
- MIN-only fragments `z54-lat-*/{m0-f52,m0-f70,m0-f100,hh-fsu52}.conf` + `eattlat-*` equivalents build a
  silent-150 image if used WITHOUT `fsu-open.conf`. Add a header comment / recipe guard.

## Methodology lesson (why a mid-stream capture hides a broken FSU)
The FSU confirmation line prints **once at connection setup** (CoC `Q3FSU-DONE`, GATT `FSU: updated
spacing=`, ~t=1-6 s from boot). `run-all.sh` and the A/B harness open serial mid-stream → they **never
see it** and cannot tell `spacing=52` from `spacing=150`. Any "verify FSU engaged" gate MUST capture
from boot. (This is *how* the broken sink went unnoticed: throughput alone looked plausible.)

## Checked, NOT bugs (so we don't cry wolf)
- **The nRF52 (BLE 5) pre-FSU apps having no FSU floor is BY DESIGN, not a defect.** The older
  benchmarks were nRF52↔nRF52; the nRF52 hardware/firmware combo does not support FSU (a Bluetooth 6
  feature). Those apps are legitimate BLE 5 bandwidth/latency benchmarks — any audit that flags an
  nRF52 app as "floor-less" is a false alarm. FSU capability begins at the nRF54L15 (BLE 6) silicon.
- **EATT campaign at IFS=150 is correct.** `eatt.conf`/`eatt-off.conf` toggle **EATT multi-bearer**
  on/off (`BT_EATT=y`, `EATT_MAX=4`), NOT FSU. The latency arm (ON 141/152 ms vs OFF 193/193 ms) never
  claimed FSU, so `EVENT_IFS_LOW_LAT_US=150` in `eatt-latency-20260826/.../p2*.config` is expected, not a
  silent-drop bug. (Worth a one-line "FSU not engaged here" note in that FINDINGS so it isn't misread.)
- **`coc-duplex-sink/open-fsu.conf` abbreviation is cosmetic.** It omits some lines the CoC template
  carries, but `BT_CTLR_EXTENDED_FEAT_SET`/`FRAME_SPACE_UPDATE` are `default y if` their host symbols
  (outside the gated menu) and the committed build resolved the floor `=y`. Only the *recipe* (:205)
  omitted the overlay.

## Genuine OPEN items (do not overstate before external use)
- **Prior duplex-FSU-ABBA** (`duplex-fsu-abba-20260824`, "grows with interval"): archived *central*
  config has the floor (`central-fsuON-example.config`: `EVENT_IFS_LOW_LAT_US=52`,
  `CONN_INTERVAL_LOW_LATENCY=y`), but the peripheral is `periph-gecho.hex` with **no archived `.config`**
  → periph floor not statically confirmable. The fresh 2026-09-07 build proves the *config* engages
  `spacing=52`; the old number's periph provenance should be re-verified before quoting it externally.
- **1M periph drift:** `apps/misc/q2-periph/fsu.conf` is **missing `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`**
  that its 2M sibling and every proven periph config pin — and auto-update is the documented mechanism
  that *silently reverts a negotiated FSU to 150*. May be intentional (commit `311d51e`); verify against
  the 1M-accept lineage before trusting any 1M result built from this exact file.

## Fixes applied 2026-09-07 (this audit)
- `REPRODUCE.md` duplex recipe: dropped the undefined `-DCONFIG_APP_SDU_SIZE=480` (build-abort) and
  added `open-fsu.conf` to the sink build + a from-boot `spacing=52` verify note.
- `REPRODUCE.md` GATT FSU on/off: documented that clean-isolation holds for CoC but NOT the GATT arms
  (off-arm drops the whole overlay); added the `BENCH_FORCE_FEAT` disclosure + the from-boot verify gate.
- `REPRODUCE.md` GATT-SDC: aligned recipe interval (units=20 = 25 ms) to the shipped `-25` hex label +
  warned against cross-comparing the open (7.5 ms) vs SDC (25 ms) GATT arms at mismatched intervals.
- `docs/LESSONS.md`: added the from-boot-capture gate, the map-by-provenance-not-name rule, and the
  recipes-not-build-tested lesson.
- Archived correctly-built duplex firmware + `.config` + SHA256SUMS + boot captures under
  `duplex-fixed-firmware/` and `captures/` (nothing left only in /tmp).

## A-Z REPRODUCE.md reader-walk (2026-09-07) — surprises found + fixed
Mechanical check of all 33 recipes (missing app/overlay/hex, undefined `-D` symbol abort): clean after
the duplex fix. Two semantic reader-walks (onboarding + recipes/claims) found:

FIXED:
- **coclat-sink (§11.2) & coclat2-sink (§11.3) built with NO `open-fsu.conf`** → both latency-under-load
  sections were characterized at tIFS=150 while labeled FSU-on (same silent-150 class as the duplex sink;
  the fix hadn't propagated). Added `open-fsu.conf` to both sink recipes + a "verify `fsu=52`" note.
- **`run-all.sh` couldn't confirm FSU** (opened serial mid-stream after `sleep 6`, missing the once-at-boot
  `spacing=52` line → `fsu52` column blank on a GOOD run). Restructured to capture from the reset instant
  and grep both logs. Added an explicit nRF54-only SCOPE header.
- **Sanity check "peripheral shows `spacing=52`" is impossible for GATT** (the GATT responder emits no FSU
  telemetry). Scoped it: CoC verifies on the sink, GATT verifies on the central's `FSU: updated` line.
- **Mutable `fsu-m0` branch** (planned repoint v4.4.1→v4.4.2 would silently swap host gatt.c/att.c). Added
  an immutable-pin recipe (`--mr 8f44a3d7a3f`). Verified remote `fsu-m0` is STILL v4.4.1; `fsu-m0-v442`
  exists, `fsu-m0-v441` not yet.
- **Fork HEAD SHA self-contradiction** — patch-series README §24 said `ea6c334d132` (matches nothing);
  corrected to the real HEAD `8f44a3d7a3f`.
- **`python3 analyze.py`** → `tools/analyze.py`. **EATT payload** clarified (≤242 = payload+3 ≤ 245).
- **Stale "open item"** — on-air FSU listed as an unresolved gap though it's FORMALLY ACCEPTED; reworded
  so only the independent pro-analyzer qualification reads as open.
- **GATT-SDC responder** was missing `SDC_ENABLE_LOWEST_FRAME_SPACE=y` (CoC-SDC sink has it) → SDC-GATT FSU
  delta UNDERSTATED (conservative, not an over-claim). Added `z54-lat-periph/sdc-fsu.conf` + recipe note;
  SDC semantics unverifiable in-repo → flagged for re-measurement.

VERIFIED CLEAN (stated so the bill of health is explicit): no hardcoded board IDs in run-all's auto-detect;
capture-tool.py args match docs; all 15 app paths exist with matching descriptions; all verification log
tokens are real; all CoC FSU overlays carry the full floor both ends; the ADVANCED_FEATURES "visible if"
menu-gate does NOT actually clamp EVENT_IFS_LOW_LAT_US/CONN_INTERVAL_LOW_LATENCY (no such Kconfig dep — the
overlay comment is inaccurate but harmless, and every overlay sets ADVANCED_FEATURES=y anyway); every
provenance/other-measurement path exists.

## Still TODO / flagged for the user
- Optionally: build a clean GATT FSU-off arm (keep F1–F4, pin MIN=MAX=150) for an attributable GATT delta.
- Re-verify the prior duplex-ABBA periph floor; resolve the q2-periph 1M auto-update drift.
- Re-measure the SDC-GATT FSU delta with `sdc-fsu.conf` (needs NCS toolchain + HW).
- Pre-FSU nRF52 apps intentionally NOT reproduced now (per user); documented in `docs/APP-PROVENANCE.md`.
