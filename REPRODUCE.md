# Reproduce the BLE 6.0 teleop-backup benchmarks

Recipes and checks for rebuilding and re-running the throughput / latency / FSU measurements,
with pointers to the separate soak, observer, and physical-STOP campaigns. RF results require the
physical rig; these instructions cannot reproduce them without it.

> **Scope:** the two-board throughput recipes use nRF54L15-DKs. Formal on-air FSU verification also
> needs an nRF52 raw-radio observer; physical-STOP timing needs its own watchdog/logic-analyzer rig.
> Earlier **nRF52 (Bluetooth 5)** findings are in
> [`apps/nrf52/nrf52-l2cap-echo/`](apps/nrf52/nrf52-l2cap-echo/README.md) and catalogued in
> [`docs/APP-PROVENANCE.md`](docs/APP-PROVENANCE.md) (pre-FSU section).

## Quick start: choose a valid campaign
For current FSU deltas, build the **matched on/off arms and shared FSU-capable sink** in
*GATT one-way* below (or the corrected CoC recipe), run the resolved-config design gate, then
capture reset-isolated, post-negotiation windows. Check each result's status and exact evidence
recipe in [EVIDENCE-INDEX](docs/EVIDENCE-INDEX.md) and the known traps in
[LESSONS](docs/LESSONS.md) first. Do not use the August 2026 prebuilt matrix to reproduce current
FSU headlines: its CoC replay gave a subsequently retracted near-zero gain. The old no-toolchain
runner remains an **archival hardware smoke test only**:
```
./run-all.sh
```
It requires 2× nRF54L15-DK, `nrfutil`, Python + pyserial, and reports capture/gate failures, not
headline comparisons. Override board detection with `CEN_ID/PER_ID/CEN_TTY/PER_TTY` if needed.
Even valid deltas depend on operating point and RF arrangement; do not expect absolute KB/s parity.

**All current matched-arm campaigns, one command** (builds every arm itself, gates every rep, runs
unattended; each step is one of the per-result tools documented below):
```
./run-campaigns.sh --dry-run          # the plan and time estimate (~7.5 h for everything)
./run-campaigns.sh --smoke            # one rep per arm, fewer intervals (~1.5 h): wiring check
./run-campaigns.sh                    # everything; or --only gatt-open,lat-open / --skip-sdc / --skip-observer
```
It needs the patched Zephyr tree (`ZEPHYR_BASE`, `west`), `nrfutil`, Python + pyserial, an NCS v3.4.0
workspace for the SDC steps (`--ncs-root`), and the nRF52 observer for the on-air steps. A step
reports PASS (every rep accepted), DONE-WITH-REJECTS (finished; some reps rejected by a gate; read its
summary) or FAIL. Compare FSU deltas and verdicts with the evidence dirs, not absolute rates.

---

## ⚠️ Before you start — read this or you'll get plausible-but-wrong numbers

Most failure modes here **don't error out** — they build, flash, and run, then hand you
confident wrong data. Front-load these:

1. **The open-controller FSU results REQUIRE a patched Zephyr — this is not optional.**
   `open-fsu.conf` sets `CONFIG_BT_CTLR_FRAME_SPACE_UPDATE`, which **does not exist in stock
   Zephyr.** Get the patched tree one of two ways:
   - **Turnkey (ready-made fork):** `west init -m https://github.com/teleop-bench/zephyr --mr <branch> zephyrproject`, then `git -C zephyrproject/zephyr checkout <campaign-commit>` and `cd zephyrproject && west update`. `west init --mr` accepts only a branch or tag, not a commit SHA. Pin the commit from the campaign's evidence, not the moving branch tip. The **v4.4.1 line** is branch `fsu-m0` (tip `8f44a3d7a3f41ef…`); the **clean September GATT recipe below was measured on v4.4.2-16**, branch `fsu-m0-v442`, commit `fee9fbc620959b218cda34602d7653d21f7f6a51`. These are different trees; do not claim exact reproduction of one with the other.
   - **From patches (audit path, v4.4.1 line only):** check out **Zephyr v4.4.1** and `git am` the 16 `fsu-m0` patches (`zephyr-patches/fsu-m0-series/*.patch`) onto it — see that [README](./zephyr-patches/fsu-m0-series/README.md).

   Skip this and the open-FSU builds fail with a cryptic Kconfig error; build them another way and FSU silently won't apply.
2. **Board is `nrf54l15dk/nrf54l15/cpuapp`.** The configs and overlays are board-specific.
3. **SDC builds need nRF Connect SDK v3.4.0**, and must run inside the NCS toolchain **in a
   clean environment** — do **not** just `source zephyr-env.sh` and build, or you'll poison the
   SDC build (we hit `BT_LL_SOFTDEVICE undefined`). Use a subshell that unsets `ZEPHYR_BASE`:
   ```
   REPO="$PWD" # run from this repository; set NCS_ROOT to your NCS workspace
   ( cd "$NCS_ROOT" && unset ZEPHYR_BASE && \
     nrfutil toolchain-manager launch --ncs-version v3.4.0 -- \
     west build -p always -b nrf54l15dk/nrf54l15/cpuapp "$REPO/apps/coc/coc-central" -d "$REPO/build/sdc-coc-on-25" -- -DEXTRA_CONF_FILE="sdc-sel.conf;sdc-fsu.conf" -DCONFIG_APP_CONN_INT_UNITS=20 )
   ```
4. **Always build pristine (`-p always`) into a distinct `-d` directory for each arm.** Reusing a build dir across different configs leaves a
   stale `.config` and you measure the wrong thing (reused SDC dirs also throw "Kconfig warnings
   fatal").
5. **Your absolute KB/s will NOT match ours** — throughput depends on your RF environment
   (distance, interference, orientation). Compare *matched, repeated deltas* at the same operating
   point; even these are regime- and session-dependent. Open-vs-SDC is not blanket parity.

## Toolchain prerequisites
- **Zephyr build env.** You need `west` in a Python venv, the **Zephyr SDK** (`arm-zephyr-eabi`), and a
  Zephyr workspace. **New to Zephyr?** do the one-time
  [Zephyr Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
  first. From zero:
  ```
  python3 -m venv .venv && . .venv/bin/activate && pip install west   # west, in a venv
  #  ...get the patched tree — see "Steps to recreate", step 1 (pick the branch with --mr, then check out the campaign commit)...
  west packages pip --install     # Python deps that 'west update' does NOT fetch
  west sdk install                # installs the Zephyr SDK (arm-zephyr-eabi)
  ```
  Then `export ZEPHYR_BASE=<zephyrproject/zephyr>` (and `ZEPHYR_SDK_INSTALL_DIR` if the SDK is elsewhere).
  Verify: `west --version` + `west sdk list`.
- **`nrfutil`** (Nordic) for flashing + `nrfutil device reset` — on PATH.
- **Python 3.9+ + `pyserial`** (`pip install -r requirements.txt`) for the capture and campaign tools; it is their only
  third-party dependency.
- **NCS v3.4.0** only if you also build the SDC (proprietary) arm — via `nrfutil toolchain-manager`.
- macOS/Linux host with USB. (This repo's runs: macOS + Zephyr SDK 1.0.1 + nrfutil + pyserial 3.5.)

## Steps to recreate (end-to-end)
The prebuilt hexes are useful for an archival smoke test, not current headline deltas. For a
current comparison, build and gate both arms. Run these steps from this repository unless noted.
1. **Get the patched Zephyr** (needed for open-FSU builds; skip if only flashing prebuilt hexes). All results are pinned
   to v4.4.2 + the fsu-m0 series. Do not rebase onto Zephyr 4.5 / `main` and expect identical CoC numbers: 4.5 moves L2CAP
   receive work to the Bluetooth workqueue (`22896cb8d6f2`), changing credit-return timing, and removes the Kconfig
   options `BT_CONN_TX_MAX`, `BT_AUTO_PHY_UPDATE` and `BT_RECV_CONTEXT` that some app configs still set (inert here).
   **turnkey** — for the clean GATT campaign below (if `ZEPHYR_BASE` is already set from another Zephyr install,
   `unset ZEPHYR_BASE` first, or `west init` refuses and points at that other workspace):
   ```
   west init -m https://github.com/teleop-bench/zephyr --mr fsu-m0-v442 zephyrproject
   git -C zephyrproject/zephyr checkout fee9fbc620959b218cda34602d7653d21f7f6a51
   cd zephyrproject && west update
   ```
   (`--mr` takes a branch or tag only; for another row use its branch and evidence-specific commit);
   **or from patches for the August v4.4.1 line only** — check out **Zephyr v4.4.1** and
   `git am zephyr-patches/fsu-m0-series/*.patch` (16 patches), see that
   [README](./zephyr-patches/fsu-m0-series/README.md). A v4.4.1 patch application is **not** the
   exact v4.4.2 clean-GATT build. Set `ZEPHYR_BASE` to the selected workspace's Zephyr tree for
   open builds; unset it in the NCS SDC subshell. Return to this repository before the recipes.
2. **Pick a result row** from *Build recipes* (or the *2026-08-25 re-test recipes*) and `west build`
   the central + peripheral with the shown `-DEXTRA_CONF_FILE=…` + `-DCONFIG_APP_*` flags.
3. **Find your board dev-ids** (`nrfutil device list`; serial ports are `/dev/cu.usbmodem*` on
   macOS, `/dev/ttyACM*` on Linux); note which is central vs peripheral.
4. **Flash peripheral first, then central:** `west flash -d <builddir> --dev-id <SN> --no-rebuild`;
   for prebuilt hexes use `nrfutil device program --firmware <hex> --serial-number <SN>` directly.
   Verify the actual flashed image/boot banner; SDC is a sysbuild image.
5. **Start capture before reset** with `tools/capture-tool.py <seconds> /dev/cu.<cen>:<run>-cen /dev/cu.<per>:<run>-per`
   (Linux: `/dev/ttyACM<n>`). Each port writes `<label>.log` and **overwrites** an existing file of
   that name, so give every run a unique `<run>` label.
   Let it open both serial ports, then reset peripheral and central with
   `nrfutil device reset --serial-number <SN>`. This captures the one-time boot/FSU messages and
   clears latched state; opening the ports may itself reset a board. Output goes to
   `$CAP_OUTDIR` if set, else `./captures/` (auto-created) — no hardcoded path (set `CAP_OUTDIR=<dir>`
   to control it).
6. **Gate each run:** `python3 tools/verify_run.py <per.log> --cen <cen.log> --fsu on|off
   --stack open|sdc --interval-ms <ms>`; reject any failure. Also inspect the *Sanity checks*
   below and record interval, SDU, PHY, DLE, spacing, distance, and firmware/config identity.
7. **Analyze the post-FSU window:** `tools/analyze.py` is a broad log summary, not a matched-pair
   or post-onset acceptance gate. For the clean GATT sweep, use the windowing in the cited harness;
   do not mix the pre-negotiation samples into an on/off delta.

## Hardware
- **2 × Nordic nRF54L15-DK** (BLE 5/6) — the central + peripheral under test.
  <https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK>
- **1 × nRF52 DK (nRF52832, PCA10040)** — the passive on-air observer / sniffer (tIFS verification);
  build target `nrf52dk/nrf52832`.
- **Logic analyzer** (e.g. SparkFun TOL-18627) — physical STOP-time confirmation.

Serial capture uses the DK's onboard J-Link CDC. Board dev-ids in this repo's runs were
`1057794857` (central) and `1057719509` (peripheral); yours will differ — find them with
`nrfutil device list` or the `/dev/cu.usbmodem*` device nodes.

## Software / controllers
- **Open Zephyr controller:** upstream **Zephyr v4.4.1** (annotated tag), base commit
  `1f6485eca25431b5ff27ce9a754218c9e559bbbb`, plus the **FSU controller patch series** (16 commits).
  The August v4.4.1 build is published as **`fsu-m0` at `8f44a3d7a3f`** in
  [github.com/teleop-bench/zephyr](https://github.com/teleop-bench/zephyr/tree/fsu-m0), or assemble from
  [`zephyr-patches/fsu-m0-series/`](./zephyr-patches/fsu-m0-series/README.md) via `git am` onto `v4.4.1`
  (see that README + [provenance](./zephyr-patches/fsu-m0-series/provenance/PROVENANCE.md)).
  *Boot banners in August `debug-evidence/` logs cite pre-publication commit SHAs: `g9999e0404460`
  (v4.4.1-14) is tree-identical to fork commit `28789805a1` (patch 14), and `g1772af7b563e`
  (v4.4.1-15) is tree-identical to fork commit `50334aaec8` (patch 15). Those builds predate the later
  patches (15–16, both default-off diagnostics); the SHAs differ only because the commits were
  re-authored to the project identity after measurement.*
  **Do not generalize that base to later campaigns.** The clean September GATT/FSU campaign uses
  `fee9fbc620959b218cda34602d7653d21f7f6a51` (`v4.4.2-16`); inspect the evidence row and
  firmware/config hashes for the specific result being reproduced.
  The FSU controller code itself is unmerged upstream WIP (PRs #82324 → #99473); this series
  imports and defect-fixes it into a working, on-air-verified state.
- **Proprietary SoftDevice Controller (SDC):** **nRF Connect SDK v3.4.0**, selected by
  `CONFIG_BT_LL_SOFTDEVICE=y` (`sdc-sel.conf`). Built via
  `nrfutil toolchain-manager launch --ncs-version v3.4.0 -- west build ...`.
- Board target: `nrf54l15dk/nrf54l15/cpuapp`.
- **Upstream Zephyr doc references** (host/controller/L2CAP/Kconfig, mapped to each finding & knob):
  [`zephyr-doc-references.md`](./docs/references/zephyr-doc-references.md).

## Applications (all in this repo)

Section references such as §4, §5 and §11.x in this file point to
[`docs/overviews/coc-technical-overview.md`](docs/overviews/coc-technical-overview.md).

> **Which app produced which result?** See [`docs/APP-PROVENANCE.md`](docs/APP-PROVENANCE.md) — the
> canonical result→app map. The app name is NOT a reliable guide (the headline GATT throughput/FSU
> result comes from the *latency*-named `z54-lat-central`, not from `z54-gatt-*`). It also lists the
> pre-FSU nRF52 / z44 (BLE 5) bandwidth & latency benchmarks — where FSU is N/A because the nRF52
> hardware doesn't support it, not because it was omitted.

| app | dir | role |
|---|---|---|
| GATT central (throughput / RTT / latency-under-load driver) | [`z54-lat-central/`](apps/nrf54l15/z54-lat-central/) | central |
| GATT peripheral (sink / echo / bulk-sink) | [`z54-lat-periph/`](apps/nrf54l15/z54-lat-periph/) | peripheral |
| GATT downlink writer (CoC-vs-GATT comparison) | [`z54-gattdl-central/`](apps/nrf54l15/z54-gattdl-central/) | central |
| GATT downlink sink (comparison) | [`z54-gattdl-dk/`](apps/nrf54l15/z54-gattdl-dk/) | peripheral |
| L2CAP CoC central (one-way throughput) | [`coc-central/`](apps/coc/coc-central/) | central |
| L2CAP CoC sink (seg_recv) | [`coc-sink/`](apps/coc/coc-sink/) | peripheral |
| CoC **duplex** central / sink (§5) | [`coc-duplex-central/`](apps/coc/coc-duplex-central/) · [`coc-duplex-sink/`](apps/coc/coc-duplex-sink/) | both |
| CoC **latency-under-load** (§11.2): stop-signal + bulk, one channel | [`coclat-central/`](apps/coc/coclat-central/) · [`coclat-sink/`](apps/coc/coclat-sink/) | both |
| CoC **two-channel** (§11.3 priority-lane): bulk + control on separate channels | [`coclat2-central/`](apps/coc/coclat2-central/) · [`coclat2-sink/`](apps/coc/coclat2-sink/) | both |
| GATT **EATT** latency-under-load (per-signal ATT bearer): stop-signal + bulk, EATT on/off | [`eattlat-central/`](apps/eatt/eattlat-central/) · [`eattlat-periph/`](apps/eatt/eattlat-periph/) | both |
| On-air observer / sniffer | [`pca10040-radio-observer/`](apps/nrf52/pca10040-radio-observer/) | nRF52 sniffer |

> **Archival CoC apps** (not used by the current campaign tools): `apps/nrf54l15/z54-uplink-central`, `z54-sink`,
> `apps/z44/*`. Their credit handling was fixed on 2026-10-07 (initial window in the connection request, stale
> `rx.credits` dropped); the sinks still return one credit per segment, which is harmless at 7.5 ms without FSU (how
> they were used) but costs exchanges with FSU at short intervals. `apps/z44/z44-repro-initiator` is deliberately left
> buggy: it is the minimal reproduction of the Zephyr 0-initial-credit stall (`-DCREDITS_IN_REQUEST=1` builds the
> correct pattern). See `debug-evidence/zephyr-l2cap-zero-credit-repro-20261007/`.

The GATT and CoC central apps are **SDC-safe** (controller-specific externs guarded), so the
same source builds on both the open and SDC controllers — the config overlay selects which.

## Build recipes

Set `B=nrf54l15dk/nrf54l15/cpuapp` and `REPO="$PWD"` from this repository. Open builds use a
normal `west build`; SDC builds run from the NCS workspace in the NCS toolchain launcher with
`ZEPHYR_BASE` unset. Always use an absolute app path and a **new `-d` path for each arm**.
`-DCONFIG_APP_CONN_INT_UNITS=<units>` pins the connection interval
(N × 1.25 ms: 6=7.5, 12=15, 20=25, 30=37.5, 40=50, 60=75, 80=100 ms).

### GATT one-way  (central = z54-lat-central, peripheral = z54-lat-periph sink)
**Clean matched-arm isolation (confound-free, 2026-09-08 re-run). The ONLY difference between on and off
is the requested frame space (52 vs 150 µs); BOTH arms keep the full FSU feature package and share ONE
FSU-capable, auto-update-OFF sink. The old off-arm that dropped `fsu-open.conf` is INVALID (confounded — 36
differing config symbols) — do NOT use it.** Fully scripted in
[`debug-evidence/gatt-fsu-clean-20260908/`](debug-evidence/gatt-fsu-clean-20260908/) (`build_gattclean.sh`
builds all arms + runs the design gate; `harness.py` runs the gated, post-FSU-windowed sweep). Results:
[EVIDENCE-INDEX](docs/EVIDENCE-INDEX.md) `gatt-fsu-clean-20260908` + the [technical report](docs/investigations/fsu-benchmark-technical-report.md).
Pin the open-controller tree to **`fee9fbc620959b218cda34602d7653d21f7f6a51`**, not the
August v4.4.1 commit, when comparing to that campaign.
The following is a copyable **15 ms** example. Run it from this repository with `west`/Zephyr SDK
available; set `NCS_ROOT` to your NCS v3.4.0 workspace for the SDC part. Substitute `N=6/20/30/40`
for 7.5/25/37.5/50 ms and use fresh output directories for each interval. Do not pass the central's
`CONFIG_APP_CONN_INT_UNITS` symbol to the peripheral (it has no such symbol).
```bash
set -e
REPO="$PWD"
B=nrf54l15dk/nrf54l15/cpuapp
N=12 # 15 ms; both arms at the same interval
for arm in on off; do
  if [ "$arm" = on ]; then overlay=hh-fsu52.conf; else overlay=hh-fsu150.conf; fi
  west build -p always -b "$B" "$REPO/apps/nrf54l15/z54-lat-central" \
    -d "$REPO/build/gatt-open-$arm-$N" -- \
    -DEXTRA_CONF_FILE="tput-open.conf;fsu-open.conf;$overlay" -DCONFIG_APP_CONN_INT_UNITS="$N"
done
west build -p always -b "$B" "$REPO/apps/nrf54l15/z54-lat-periph" \
  -d "$REPO/build/gatt-open-sink" -- -DEXTRA_CONF_FILE="tput-open.conf;fsu-open.conf"
python3 tools/check_matched_pair.py \
  "build/gatt-open-on-$N/zephyr/.config" "build/gatt-open-off-$N/zephyr/.config" \
  --allow CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US
grep -Eq 'CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n|# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set' build/gatt-open-sink/zephyr/.config
grep -Eq 'EVENT_IFS_LOW_LAT_US=52' build/gatt-open-sink/zephyr/.config

# SDC: use the SAME N; NCS_ROOT must be the absolute path of the NCS workspace.
: "${NCS_ROOT:?set NCS_ROOT to your NCS v3.4.0 workspace}"
for arm in on off; do
  if [ "$arm" = on ]; then overlay=hh-sdc-fsu.conf; else overlay=hh-sdc-nofsu.conf; fi
  ( cd "$NCS_ROOT" && unset ZEPHYR_BASE &&
    nrfutil toolchain-manager launch --ncs-version v3.4.0 -- \
      west build -p always -b "$B" "$REPO/apps/nrf54l15/z54-lat-central" \
      -d "$REPO/build/gatt-sdc-$arm-$N" -- \
      -DEXTRA_CONF_FILE="sdc-sel.conf;tput.conf;$overlay" -DCONFIG_APP_CONN_INT_UNITS="$N" )
done
( cd "$NCS_ROOT" && unset ZEPHYR_BASE &&
  nrfutil toolchain-manager launch --ncs-version v3.4.0 -- \
    west build -p always -b "$B" "$REPO/apps/nrf54l15/z54-lat-periph" \
    -d "$REPO/build/gatt-sdc-sink" -- \
    -DEXTRA_CONF_FILE="sdc-sel.conf;tput.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf" )
python3 tools/check_matched_pair.py \
  "build/gatt-sdc-on-$N/z54-lat-central/zephyr/.config" \
  "build/gatt-sdc-off-$N/z54-lat-central/zephyr/.config" \
  --allow CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US
grep -Eq 'CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n|# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set' build/gatt-sdc-sink/z54-lat-periph/zephyr/.config
grep -Eq 'SDC_ENABLE_LOWEST_FRAME_SPACE=y' build/gatt-sdc-sink/z54-lat-periph/zephyr/.config
```
Stop if any build/gate fails. Flash each central with its stack-matched shared sink (SDC hexes are
under `<build>/z54-lat-{central,periph}/zephyr/`). Reset and capture **from boot** for each arm;
run `verify_run.py` on each capture. For throughput, use a late window clipped to begin strictly
after the central's FSU-onset timestamp, not the whole-capture median. The archived
[`harness.py`](debug-evidence/gatt-fsu-clean-20260908/harness.py) implements the post-onset window
and repeated, counterbalanced sweep; its original absolute paths/board IDs must be adapted to
your rig and the archived file must not be edited.

### GATT duplex FSU (matched arms)  (central = z54-lat-central, peripheral = z54-lat-periph **echo**)
The central blasts write-without-response; the peripheral echoes each write back as a notification.
**Aggregate** = central `exkBps` (echo received) + peripheral `rxkBps` (blast received). The echo
couples the two directions, so only the aggregate is a result; the per-direction split is not a
symmetry measure (use an independent bidirectional rig for that).

**One command** (builds, design-gates, flashes, captures, analyzes; ABBA n=4 per arm per interval):
```
python3 tools/gatt-duplex-fsu.py --stack open --out <dir> --smoke    # one rep per arm first
python3 tools/gatt-duplex-fsu.py --stack open --out <dir>            # 7.5 + 25 ms (--intervals 6,20)
python3 tools/gatt-duplex-fsu.py --stack sdc  --out <dir> --ncs-root <NCS v3.4.0 workspace>
python3 tools/gatt-duplex-fsu.py --stack open --out <dir> --diag     # + exchanges per connection event
```
What it does, and the traps it designs out:
- **Matched arms.** Central overlays `tput-open.conf;fsu-open.conf` + `hh-fsu52.conf` (on) or
  `hh-fsu150.conf` (off); SDC: `sdc-sel.conf;tput.conf` + `hh-sdc-fsu.conf` / `hh-sdc-nofsu.conf`.
  Both arms run the FSU procedure; `check_matched_pair.py` must report MATCHED before anything is flashed.
- **Shared FSU-capable echo peripheral, auto-update off.** Open: `tput-echo-open.conf;fsu-open.conf`;
  SDC: `sdc-sel.conf;tput-echo.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf`; both with
  `-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`, checked in the resolved config.
- **Per rep:** capture from boot. FSU-on must log `FSU: updated … spacing=<150` (open grants 52 µs,
  SDC 70 µs). FSU-off must log `FSU: request [150..150] rc=0` and never a reduced spacing (a 150
  request on a 150 link is a no-op, so no "updated" line). Interval must equal the requested one;
  no disconnects; FSU held on both directions (`verify_run.fsu_held`).
- **Window:** FSU completion (FSU-off: the request) + 2 s to the end of the capture (45 s open; 60 s
  SDC, whose FSU starts at ~15 s after a built-in 12 s delay); medians. Summary gives the FSU gain
  with a Welch 95% interval and whether the arms' ranges overlap.
- **`--diag`** (open only) builds the centrals with `CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y`; the
  central then prints the controller's transactions-per-event histogram (`RPT pe:`, from
  `src/eventfill.c`, compiled only in diag builds) and the tool reports exchanges per event per arm.
  Diag builds use a separate build dir; quote throughput from non-diag runs.

Results: [`gatt-duplex-fsu-matched-20261003`](debug-evidence/gatt-duplex-fsu-matched-20261003/README.md) (open,
7.5/25 ms) and [`gatt-duplex-fsu-model-20261003`](debug-evidence/gatt-duplex-fsu-model-20261003/README.md) (SDC,
15 ms, and the per-event exchange counts that explain the pattern); model tests
[`duplex-fsu-followups-20261003`](debug-evidence/duplex-fsu-followups-20261003/README.md) and
[`model-prereg-20261004`](debug-evidence/model-prereg-20261004/README.md); second session
[`replication-20261004`](debug-evidence/replication-20261004/README.md).

### CoC duplex FSU (matched arms)  (central = coc-duplex-central, peripheral = coc-duplex-sink)
Two independent CoC streams, one each way (not an echo). **Aggregate** = sink `SINK rx` (downlink) +
central `CENRX cum_total` slope (uplink); per-direction rates are reported too.
```
python3 tools/coc-duplex-fsu.py --stack open --out <dir> --smoke     # one rep per arm first
python3 tools/coc-duplex-fsu.py --stack open --out <dir>             # 7.5/15/25 ms, ABBA n=4 per arm
python3 tools/coc-duplex-fsu.py --stack sdc  --out <dir> --ncs-root <NCS v3.4.0 workspace>
```
- **Procedure:** after flashing each central image the tool discards the first connection; every measured
  rep opens the capture first and then resets **only the peripheral**, so the connection is a same-boot
  reconnection and its FSU lines are on record. A rep in which the central rebooted is rejected; a
  stalled direction is counted as a stall, never averaged in. (This began as the workaround for the
  "reconnect wedge"; since 2026-10-06 the wedge's trigger is fixed in the apps, see below.)
- **CoC credit handling (fixed 2026-10-06; check any CoC app that reconnects without rebooting both boards):**
  reset credit counters wherever a fresh window is granted; zero a reused `seg_recv` channel's `rx.credits`
  before connect/accept (the host keeps the old count and sends it as initial credits); and give the initial
  window **before** `bt_l2cap_chan_connect`. Opening with 0 initial credits trips a Zephyr host bug that can
  stall the peer's TX forever (the "reconnect wedge"). `debug-evidence/coc-credit-fixes-20261006/`.
- **Arms:** open `open-fsu.conf` vs `open-nofsu.conf`; SDC `sdc-sel.conf;sdc-fsu.conf` vs
  `sdc-sel.conf;sdc-nofsu.conf` (`check_matched_pair.py` must report MATCHED). Sink: open `open-fsu.conf`,
  SDC `sdc-sel.conf;sdc.conf`, both with `-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`.
- **FSU check:** FSU-on needs `Q3FSU-DONE role=C … spacing<150` (open: also the peripheral's `role=P` line);
  FSU-off needs `Q3FSU-REQ … min=150 max=150 rc=0` and never a reduced spacing.
- **Open stack: 20-deep controller TX queues** (`CONFIG_BT_BUF_ACL_TX_COUNT=20`, `CONFIG_BT_ATT_TX_COUNT=64`, now in the
  apps' `open-*.conf`; SDC's controller holds 20 packets). With prj.conf's 64, credit returns wait behind ~64 queued
  packets and at 25 ms both sides periodically run dry (`debug-evidence/coc-duplex-25ms-diag-20261006/`).
- Report the per-direction rates with the aggregate; with the fixed apps both stacks split ~1:1. (The
  published ~1:2 open split was the credit leak; a 64-deep sink controller queue then delayed the credit
  returns, `debug-evidence/coc-duplex-credit-trace-20261006/`.)
- `--abba N` sets the ABBA blocks per interval (default 2 → n=4 per arm). `--cold` (reset both boards each rep)
  works with the fixed apps (it stalled most reps before). `--cen-extra` / `--sink-extra` pass extra `-D`
  flags (e.g. a shallower controller queue). `CONFIG_APP_CREDIT_TRACE=y` on either app prints a 1 s `CTRACE`
  line of TX credits / L2CAP queue / free controller buffers, to see where a sender waits.

Results: Zephyr [`coc-duplex-fsu-q20-20261006`](debug-evidence/coc-duplex-fsu-q20-20261006/README.md) (20-deep queues), SDC [`coc-duplex-fsu-fixed-20261006`](debug-evidence/coc-duplex-fsu-fixed-20261006/README.md); second day [`replication-20261007`](debug-evidence/replication-20261007/README.md). Superseded,
bug-affected: [`coc-duplex-fsu-matched-20261003`](debug-evidence/coc-duplex-fsu-matched-20261003/README.md); n=8 re-runs and
`--cold` in [`duplex-fsu-followups-20261003`](debug-evidence/duplex-fsu-followups-20261003/README.md); second session
[`replication-20261004`](debug-evidence/replication-20261004/README.md).

### Latency under load with FSU  (central = z54-lat-central, peripheral = z54-lat-periph)
Stop-signal ping RTT under a stepping bulk load, FSU on vs off. The arms differ only in the requested frame
space (`hh-fsu52.conf` vs `hh-fsu150.conf`) on top of `loadramp.conf;fsu-open.conf` (10-buffer ACL queue);
the peripheral is built with `-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`.
```
python3 tools/latency-fsu.py --out <dir> --smoke                     # one rep per arm first
python3 tools/latency-fsu.py --out <dir>                             # 7.5 ms naive + paced, 25 ms naive; ABBA n=2 per arm
python3 tools/latency-fsu.py --out <dir2> --builds <same> --skip-build   # a second ABBA block -> n=4 when pooled
python3 tools/latency-fsu.py --high --out <dir>                      # stages 0/140/150/160/170/180/190 KB/s
python3 tools/latency-fsu.py --summarize <dir> [<dir2> ...]          # pool + re-measure from archived captures
python3 tools/latency-fsu.py --diag --out <dir>                      # open: + per-event transaction counter per stage
python3 tools/latency-fsu.py --stack sdc --ncs-root <NCS v3.4.0> --out <dir>   # SDC arms (TX packet count 10)
```
- One run = a full ramp captured from boot (7 stages × 60 s, ~7.5 min). The central prints one `PCTL` line
  per stage; the tool also records the bulk rate the peripheral actually received (`blkkBps`) per stage, so
  stages above the link's ceiling show as such. `--high` builds with `CONFIG_APP_LOAD_RAMP_HIGH=y`.
- Gates: FSU-on must log `FSU: updated … spacing=52`; FSU-off the `FSU: request [150..150] rc=0` line and no
  reduced spacing; interval and all 7 stages present. The idle stage includes the ~6 s before FSU completes.
- Report percentiles (p50 / p99 / p99.9) with the achieved bulk rate. The `>30 ms` share is threshold-
  sensitive here: loaded RTTs sit near 30 ms, so a few-ms shift flips most pings across the line.

- SDC (`--stack sdc`, NCS v3.4.0): central `sdc-sel.conf;loadramp.conf;sdc-llbuf.conf` + `hh-sdc-fsu.conf` /
  `hh-sdc-nofsu.conf`; peripheral `sdc-sel.conf;loadramp.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf`, auto-update
  off. The SDC FSU-on gate is spacing < 150 µs (SDC negotiates 70 µs), and SDC issues the FSU request ~12 s after
  connecting, still inside the idle stage. `--diag` needs the open controller.

Results: [`latency-fsu-20261003`](debug-evidence/latency-fsu-20261003/README.md); second session
[`replication-20261004`](debug-evidence/replication-20261004/README.md); mechanism (`--diag`) and SDC
[`latency-fsu-20261004`](debug-evidence/latency-fsu-20261004/README.md).

### One-way FSU, Zephyr vs SDC, same session
The cross-stack reference: GATT and CoC one-way, open Zephyr and Nordic SDC, FSU off/on, 7.5–50 ms, all in one
session with Zephyr and SDC interleaved and one verified tuning profile, so absolute rates compare across stacks.
```
python3 tools/oneway-fsu.py --smoke --out <dir> --ncs-root <NCS v3.4.0>     # 25 ms, one round (~20 min)
python3 tools/oneway-fsu.py --out <dir> --ncs-root <NCS v3.4.0>             # headroom check + sweep, n=4 (~4.5 h)
python3 tools/oneway-fsu.py --transports coc --coc-credit-batch --phase sweep --out <dir> --ncs-root <NCS v3.4.0>
                                                                             # CoC with batched credit returns (~1.5 h)
```
- Builds 64 images (recipes as documented above, plus the "max" tuning profile as `-D` flags) and refuses to flash
  unless: on/off arms are matched pairs; the profile is present in both boards' resolved configs; the Zephyr and SDC
  images of each transport agree on every host setting (host parity); sinks are FSU-capable with auto-update off.
- Headroom phase: recipe vs max tuning, FSU on, 7.5 and 50 ms. Sweep: every round visits all 40 cells with Zephyr
  and SDC adjacent; even rounds reversed. Gates per rep: FSU evidence (on: reduced spacing; off: the 150 µs request),
  `verify_run.verify`; throughput = median of the last 12 s after FSU took effect (CoC: the sink's cumulative byte
  counter over that window, since its per-second line reads ~1% high).
- **CoC: use `--coc-credit-batch`.** The recipe sink returns one credit per segment, which puts a 12-byte credit packet
  in nearly every reply and costs exchanges (7.5 ms with FSU: 0% instead of +20%; SDC 15 ms without FSU: 141 instead of
  156 KB/s). The default stays per-segment so the archived images rebuild byte-identically.
- **Record the placement before running** (a `PLACEMENT.md` in the output dir) and check that the first reps' per-second
  rates are steady: a lossy link makes a cross-stack comparison meaningless.

Results: [`oneway-crossstack-20261005`](debug-evidence/oneway-crossstack-20261005/README.md) (GATT reference; its CoC half used the per-segment sink) · CoC with batched credits: [`oneway-coc-batched-20261006`](debug-evidence/oneway-coc-batched-20261006/README.md) (second day: [`replication-20261007`](debug-evidence/replication-20261007/README.md)).

### On-air duplex check (nRF52 observer)
`tools/onair-duplex.py` tunes the observer to a GATT echo duplex link (open stack; it reads the access
address and CRC seed from the central's `Q2CONN` line) and records every packet on data channel 10 for
30 s. Per complete connection event it reports packets, full exchanges (a trailing packet that fails CRC
is counted as cut off), and the in-event gap (tIFS + ~24 µs at 2M):
```
python3 tools/onair-duplex.py --builds <gatt-duplex open build dir> --obs-build <observer 2M build> --out <dir>
python3 tools/onair-duplex.py --reanalyze <dir> [<dir> ...]          # recompute from archived logs
```
For a CoC duplex link add `--mode coc` and pass the open build dir from `tools/coc-duplex-fsu.py --stack open`;
it then discards the first connection after each central flash and resets only the peripheral per rep (the
wedge workaround), and also reports event kinds (duplex vs one-way, where one side sends only empty packets).
The GATT build dir is the one `tools/gatt-duplex-fsu.py --stack open` produces (both central arms per interval plus
the echo peripheral);
the observer build is `-DQ2=1 -DAIRTIME_MIN_TICKS=256`. In ~2% of boots the central's deferred log
buffer overflows at connection time (`--- 1 messages dropped ---`) and drops the `Q2CONN` line the observer
needs; the tool then reconnects for a fresh access address (up to 3 connections per rep, failed attempts
kept as `<tag>-tryN-*.log`). `tools/observer-smoke.sh` retries the same way. SDC links can't be observed this way: SDC doesn't
expose the connection's access address. Results:
[`onair-duplex-20261003`](debug-evidence/onair-duplex-20261003/README.md).

### L2CAP CoC one-way  (central = coc-central, peripheral = coc-sink)
Use `apps/coc/coc-central` with `open-fsu.conf` vs `open-nofsu.conf` (or
`sdc-sel.conf;sdc-fsu.conf` vs `sdc-sel.conf;sdc-nofsu.conf` in NCS). Build each arm into a
**different pristine `-d` directory**, at the same `CONFIG_APP_CONN_INT_UNITS` (12 = 15 ms) and
`CONFIG_APP_SDU_SIZE` (244 or 480 B). Build `apps/coc/coc-sink` with
`-DEXTRA_CONF_FILE=open-fsu.conf -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` for both open arms, or
`-DEXTRA_CONF_FILE="sdc-sel.conf;sdc.conf" -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` for SDC.
**The auto-update flag is required:** `coc-sink/prj.conf` prefers a 50 ms interval, and with
auto-update on the sink's ~5 s parameter update both drifts the interval and reverts FSU. Confirm in
the resolved sink `.config` that auto-update is off and the FSU floor is set
(`EVENT_IFS_LOW_LAT_US=52` open, `SDC_ENABLE_LOWEST_FRAME_SPACE=y` SDC). Run `check_matched_pair.py`
on each on/off central config; allow **only** `CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US`. The
corrected held-FSU campaign's capture harness, firmware and raw logs are in
[`coc-fsu-corrected-20260908`](debug-evidence/coc-fsu-corrected-20260908/) (its hexes were built
outside the repo; the recipe above reconstructs them). Check the ledger first.

**FSU on vs off — clean isolation now holds for BOTH CoC and GATT (enforced by the design gate):**
- **CoC (clean):** `open-fsu.conf` and `open-nofsu.conf` both run the FSU procedure and differ ONLY
  in the requested min/max frame space (52 vs 150 µs), so the *only* variable between arms is the
  granted inter-packet gap.
- **GATT (clean since 2026-09-08):** use `hh-fsu52.conf` (on) vs **`hh-fsu150.conf`** (off) for open, and
  `hh-sdc-fsu.conf` vs **`hh-sdc-nofsu.conf`** for SDC — both keep the full FSU feature package; only the
  requested spacing differs. `tools/check_matched_pair.py` **enforces** this before every run. The OLD
  off-arm that *dropped* `fsu-open.conf` is confounded (36 differing symbols incl. `BT_BUF_EVT_RX_SIZE`
  255→68) and is retained **only** as the linter's REJECT fixture (`tools/matched-pair-fixtures/`), never
  as a recipe.

**Note — `CONFIG_BT_CTLR_FSU_BENCH_FORCE_FEAT=y`** (in every `*-fsu`/`open-fsu` overlay) is a bench
shim that forces the FSU feature bit known-supported and skips the over-the-air feature-page exchange.
The *spacing reduction itself is real* (HW `spacing=52`, and on-air-observed via the nRF52 sniffer +
the 1M/2M formal-accept); the shim only bypasses capability negotiation, not the tIFS change.

**⚠️ Verify FSU is held throughout — not just engaged once** (see [LESSONS](docs/LESSONS.md) #1; this trap produced a wrong headline).
Two failure modes, both silent:
1. **Never engages** — the confirmation line (`Q3FSU-DONE … spacing=52` / `FSU: updated … spacing=52 us`)
   prints ONCE at setup (~1–6 s), so a mid-stream capture misses it and can't tell `spacing=52` from a
   clamped `spacing=150`. → capture from BOOT.
2. **Engages then silently REVERTS mid-run** — the responder's ~5 s GAP auto-param-update resets tIFS to
   150 µs, and the `fsu=52` token is **latched/stale** (keeps printing 52 after the revert). The only
   reliable signal is the **throughput time-series**: with FSU held it stays at the high plateau; on a
   revert it steps down (e.g. 188→160) and stays. → **build the responder sink with
   `-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`** (or request FSU after the final param-update), and verify
   the throughput does NOT step down.
**Automate the per-capture gate:** `python3 tools/verify_run.py <per.log> --cen <cen.log> --fsu on
--stack open --interval-ms 15` — it checks the held-throughput signature, observed spacing for open
FSU-on, link liveness, and any supplied interval. Some PHY/version checks are warnings; check those
manually. It does **not** replace the matched-config or sink-design gate, nor select a post-FSU window.
The [ledger](docs/EVIDENCE-INDEX.md) identifies the corrected campaign that supersedes the
reverted-capture CoC sweep.

**SDU size & interval are `-D` recipes, never source edits** (`CONFIG_APP_SDU_SIZE`,
`CONFIG_APP_CONN_INT_UNITS`). 244 B = 1 L2CAP segment; 480 B = 2 segments (both PDUs must arrive →
~2× loss-sensitivity, see §4). The same `-DCONFIG_APP_CONN_INT_UNITS=<u>` knob is on
`z54-gattdl-central` for the CoC-vs-GATT comparison (6=7.5 ms, 12=15 ms, 20=25 ms, 30=37.5, 40=50).

### 2026-08-25 re-test recipes (matched-config, range, FSU-vs-interval, duplex, refill)
Historical recipes on 2× nRF54L15-DK, open `fsu-m0`, 10 cm & 2 ft. These are **not**
the corrected held-FSU matched-pair campaign. Their sinks (`coc-sink`, `coc-duplex-sink`) leave
GAP auto-update **on** with a 50 ms preferred interval, so FSU can revert and the interval can drift
mid-run; to measure a held-FSU cell, add `-DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` to the sink build. Prebuilt hexes + SHA256 in
[`prebuilt-hexes/20260825/`](./prebuilt-hexes/20260825/). **Reset both boards fresh before each
capture, after opening the capture ports** (stale-tag gotcha). The commands below are recipe
templates: give every variant a separate pristine `-d` build dir; never run sequentially into
one default `build/` directory.
```
# Test A — matched config (480 B / 15 ms / FSU-on), sink = coc-sink open-fsu.conf
west build -p -b $B apps/coc/coc-central -- -DEXTRA_CONF_FILE="open-fsu.conf"   -DCONFIG_APP_SDU_SIZE=480 -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/coc/coc-central -- -DEXTRA_CONF_FILE="open-nofsu.conf" -DCONFIG_APP_SDU_SIZE=480 -DCONFIG_APP_CONN_INT_UNITS=12   # FSU-off control

# Test A' — CoC-vs-GATT range (matched 244 B / 15 ms / no-FSU): CoC-244 vs GATT-244-write
west build -p -b $B apps/coc/coc-central       -- -DEXTRA_CONF_FILE="open-nofsu.conf" -DCONFIG_APP_SDU_SIZE=244 -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/nrf54l15/z54-gattdl-central -- -DCONFIG_APP_CONN_INT_UNITS=12     # peripheral = apps/nrf54l15/z54-gattdl-dk
# measure each at 10 cm and 2 ft; compare the drop ratios (like-for-like transport check)

# Historical FSU vs interval (one-way, 480 B): sweep -DCONFIG_APP_CONN_INT_UNITS in {12,20,30,40}
#   ABBA on/off/off/on per interval, fresh capture-then-reset each run. The printed fsu=52 is
#   LATCHED, not proof of held spacing; these old binaries are not the corrected FSU campaign.

# CoC-duplex. Peripheral has AUTO_DATA_LEN (DLE=251); the stall is INTERMITTENT
# (the "251=deterministic trigger" was retracted 2026-08-26 — it flowed across 4 channel-opens).
# NOTE: coc-duplex-central has NO APP_SDU_SIZE symbol (SDU fixed via BT_L2CAP_TX_MTU=512 in prj.conf) —
# do NOT pass -DCONFIG_APP_SDU_SIZE (Kconfig aborts the build on the undefined symbol). And the SINK
# MUST get open-fsu.conf or the FSU floor is silently 150 on the peripheral end (verify spacing=52).
west build -p -b $B apps/coc/coc-duplex-central -- -DEXTRA_CONF_FILE="open-fsu.conf" -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/coc/coc-duplex-sink    -- -DEXTRA_CONF_FILE="open-fsu.conf"
# aggregate = central CENRX (uplink B->A) + sink cum_total slope (downlink A->B)
# VERIFY FSU engaged (from BOOT — the Q3FSU-DONE line prints once at setup, a mid-stream capture misses it):
#   central log must show  Q3FSU-DONE role=C status=0x00 spacing=52   (not spacing=150)

# Refill-lever investigation (§11.1); read the evidence before interpreting its result.
west build -p -b $B apps/coc/coc-central -- -DEXTRA_CONF_FILE="open-fsu.conf;deepbuf.conf" -DCONFIG_APP_SDU_SIZE=480 -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/coc/coc-sink    -- -DEXTRA_CONF_FILE="open-fsu.conf;deepbuf.conf"

# CoC latency-under-load (§11.2): stop-signal ping-pong on the SAME CoC channel as bulk.
#   central = coclat-central, sink = coclat-sink. PIN the interval (sink PREF=12 +
#   BT_GAP_AUTO_UPDATE_CONN_PARAMS=n, already set) or latency drifts. Central knobs:
west build -p -b $B apps/coc/coclat-central -- -DEXTRA_CONF_FILE="open-fsu.conf" -DCONFIG_APP_CONN_INT_UNITS=12   # saturating (pool64)
#   -DCONFIG_APP_POOL_DEPTH=4          # completion-pacing analog (shallow outstanding depth)
#   -DCONFIG_APP_NO_BULK=y             # idle floor (ping-pong only)
#   -DCONFIG_APP_OFFERED_KBPS=40|80|120|160|0   # offered-load sweep (0=saturate); the knee is AT the ceiling
# SINK MUST get open-fsu.conf too, or the FSU floor is silently 150 on the responder (its prj.conf has
# no floor) — the latency-under-load cells would then be characterized at tIFS=150, not the FSU config.
west build -p -b $B apps/coc/coclat-sink -- -DEXTRA_CONF_FILE="open-fsu.conf"
# central prints "LAT: pings=.. rtt_us[min/mean/max]=.. over30ms=.. | bulk=.."; sink prints SINK rx + echoes
# VERIFY: boot FSU completion, resolved sink config, and held-throughput trace; fsu=52 alone is latched.

# CoC two-channel PRIORITY LANE (§11.3): bulk on CoC PSM 0x0080 + stop-signal on a SEPARATE PSM 0x0081
west build -p -b $B apps/coc/coclat2-central -- -DEXTRA_CONF_FILE="open-fsu.conf" -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/coc/coclat2-sink    -- -DEXTRA_CONF_FILE="open-fsu.conf"   # sink needs the floor too (else silent 150)
# central prints "LAT2: pings=.. rtt_us[..] .. ctrl=1" (separate-channel control RTT vs shared)
# MEASURE THE SUSTAINED WINDOW (t>=10s), NOT the first saturation second.
# Re-test harness (sustained-window + Student-t): debug-evidence/coc-dedicated-lane-retest-20260909/harness.py
# Reset-recovery recipe on this rig: debug-evidence/reset-recovery-100-20260909/harness.py
# Controller pin for both re-tests: fee9fbc (v4.4.2-16); exact hexes archived in the dedicated-lane dir.

# GATT latency-under-load (the matched control comparison, §11.2/§11.3): z54-lat LOAD RAMP.
#   Use loadramp.conf (NOT tput-open.conf — that's throughput-blast mode). gdeep.conf = deep ACL queue
#   (64 buffers; this 15 ms control recipe uses it deliberately). The accepted 7.5 ms latency results
#   (hardening-20260814, latency-fsu-20261003) use loadramp.conf alone (10 buffers): at 7.5 ms gdeep
#   censors the tail at the 200 ms ping timeout. For FSU on/off see "Latency under load with FSU".
#   naive (unpaced) = loadramp.conf alone; paced arm = also set CONFIG_APP_LOAD_POLITE=y.
west build -p -b $B apps/nrf54l15/z54-lat-central -- -DEXTRA_CONF_FILE="loadramp.conf;fsu-open.conf;gdeep.conf" -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/nrf54l15/z54-lat-periph  -- -DEXTRA_CONF_FILE="loadramp.conf;fsu-open.conf;gdeep.conf"
# central prints "t=Ns RTT mean=.. >30ms=.." per "LOADRAMP: target=<KBps>" stage
```

### 2026-08-26 TX-staging investigation (per-event ceiling; result in docs/overviews)
Prebuilt hexes + SHA256 + per-hex recipe table in
[`prebuilt-hexes/20260826-tx-staging/`](./prebuilt-hexes/20260826-tx-staging/); turnkey driver
scripts (build+flash+capture+parse) in
[`debug-evidence/tx-staging-20260826/`](./debug-evidence/tx-staging-20260826/)
(`stage1.sh` interval sweep, `stage1b.sh` FSU on/off ABBA, `stage2.sh` host-handed counter,
`stage2b.sh` stall probe, `parse_stage1.py`). Sink built `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` so the
central's pinned interval holds — verify per run via the central `GATE conn: interval=` line.
```
# Stage 1 — interval sweep = count-cap-vs-airtime discriminator. DIAG on = per-event occupancy.
#   Metric: central "RPT occ: ... mean_pkts/ev=X".
west build -p -b $B apps/coc/coc-central -- -DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y -DCONFIG_APP_SDU_SIZE=480 \
  -DCONFIG_APP_CONN_INT_UNITS=6  -DCONFIG_BT_BUF_ACL_TX_COUNT=64   # =12, =20 for 15/25ms. (BT_CONN_TX_MAX is inert in v4.4.1 — omitted)
west build -p -b $B apps/coc/coc-sink    -- -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n

# Stage 1b — FSU on/off @15ms (ABBA). Metric: mean_pkts/ev delta (FSU converts tIFS 150->52us spacing to packing).
west build -p -b $B apps/coc/coc-central -- -DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y -DCONFIG_APP_SDU_SIZE=480 \
  -DCONFIG_APP_CONN_INT_UNITS=12 -DEXTRA_CONF_FILE=open-fsu.conf
# Stage 1b sink — FSU-capable + auto-update off (same symbols as stage1b.sh's explicit -D list):
west build -p -b $B apps/coc/coc-sink    -- -DEXTRA_CONF_FILE=open-fsu.conf -DCONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n

# Stage 2 — host-handed PDU counter. REQUIRES the full 16-patch fsu-m0 series (patch 0016 = l2cap_pull_pdus,
#   gated on CONFIG_BT_TESTING). Central prints "RPT stage2: host_pulls=+N pulls/ev=X" == aired 1:1.
west build -p -b $B apps/coc/coc-central -- -DCONFIG_BT_TESTING=y -DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y \
  -DCONFIG_APP_SDU_SIZE=480 -DCONFIG_APP_CONN_INT_UNITS=12 -DCONFIG_BT_BUF_ACL_TX_COUNT=64   # BT_CONN_TX_MAX omitted (inert v4.4.1)
# Peripheral DLE=251 stall probe (duplex): sink prints "... host_pulls=N" in the SINK rx line.
west build -p -b $B apps/coc/coc-duplex-central -- -DCONFIG_BT_TESTING=y
west build -p -b $B apps/coc/coc-duplex-sink    -- -DCONFIG_BT_TESTING=y
```

### 2026-08-26 EATT latency-under-load arm (does a per-signal ATT bearer help the stop-signal tail?) — canonical: preregistered-gate run 2026-08-27
**First run (2026-08-26) INVALID** (seq-correlation defect). **Canonical = preregistered-gate ABBA run**
(`debug-evidence/eatt-latency-20260826/preregistered-20260827/FINDINGS.md`, gate PASS) — see that
FINDINGS and [coc-technical-overview.md](./docs/overviews/coc-technical-overview.md) §11.4 for the
result. Driver `.../preregistered-20260827/eatt-rerun2.sh` (preregistered gate — per-line-verified by
`reanalyze.py`; timeout-inclusive analysis inline).
Apps `eattlat-central` (z54-lat + SMP Just-Works pairing + `bt_conn_set_security L2` + per-second
`eatt=N`) + `eattlat-periph`. Turnkey driver: `debug-evidence/eatt-latency-20260826/eatt-run.sh`
(builds both arms, smoke-gates on `eatt≥2` bearers, runs the ramp A/B, dumps per-stage `PCTL`).
**Corrected validity gate for any rerun** (the 2026-08-27 run used an over-strict `seqbad==0` gate —
see FINDINGS; `seqbad` also counts *correctly-discarded* late pongs after a timeout, so `seqbad>0` is
not by itself invalid): **(a) `seqbad=0` before the first timeout in every arm; (b) cumulative
`seqbad ≤ cumulative timeouts` throughout; (c) every recorded RTT is an exact seq match (guaranteed by
the fixed `notify_cb`) — report the counter as `late_pong_dropped`, not generic `seqbad`; (d) count
timeouts as budget failures (in the denominator); (e) omit p99 near 200 ms (the timeout censors it);
(f) counterbalance ABBA reset-isolated; (g) bearer assignment is unmeasured — say the mechanism is
inferred.** Read the two EATT gotchas below too (Kconfig silent-drop + the −6 bearer-MTU).
```
OV="loadramp.conf;gdeep.conf"
# EATT-on (4 ECRED bearers). eatt.conf = SMP + DYNAMIC_CHANNEL + ECRED + EATT + EATT_MAX=4 +
#   L2CAP_TX_MTU=247. Bulk payload is 240 B (must be <= 242: payload+3 B ATT header <= BT_BUF_ACL_RX_SIZE-6 = 245).
west build -p -b $B apps/eatt/eattlat-central -- -DEXTRA_CONF_FILE="$OV;eatt.conf"     -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/eatt/eattlat-periph  -- -DEXTRA_CONF_FILE="$OV;eatt.conf"
# EATT-off control: same encrypted link, single ATT bearer (eatt-off.conf = SMP only)
west build -p -b $B apps/eatt/eattlat-central -- -DEXTRA_CONF_FILE="$OV;eatt-off.conf" -DCONFIG_APP_CONN_INT_UNITS=12
west build -p -b $B apps/eatt/eattlat-periph  -- -DEXTRA_CONF_FILE="$OV;eatt-off.conf"
# verify EATT is REALLY on: central logs "SECURITY: level=2 ... eatt_bearers=" and "... eatt=4" per second;
# and grep the capture for "<err> bt_l2cap" — any hit => the MTU is wrong, discard the run.
```

### Firmware map — 2026-08-25 turnkey hexes
`prebuilt-hexes/20260825/` (+ `SHA256SUMS`, `README.md` manifest). The original rig used central J-Link
**1057794857**, peripheral **1057719509** (both nRF54L15-DK / PCA10156); use **your** serial IDs.
The nRF52 board was not used in this historical throughput set. Flash peripheral first, then central:
```
nrfutil device program --firmware prebuilt-hexes/20260825/<peripheral>.hex --serial-number <PER_ID>
nrfutil device program --firmware prebuilt-hexes/20260825/<central>.hex --serial-number <CEN_ID>
```
Key files: matched-config `coc-central-480-15ms-fsu-{on,off}.hex` + `coc-sink-open-fsu.hex`;
CoC-vs-GATT `coc-central-244-15ms-nofsu.hex`, `gatt-central-244-15ms.hex`, `gatt-peripheral-244.hex`;
duplex `coc-duplex-{central-480-15ms-fsu-on,sink}.hex`; latency `coclat-central-{pool64,pool4,idle,
offered-40/80/120/160/0}.hex` + `coclat-sink-pinned15ms.hex`.

## Run / capture / flash
- **Flash:** `west flash -d <build-dir> --no-rebuild --dev-id <board-id>` for a built image, or
  `nrfutil device program --firmware <hex> --serial-number <board-id>` for a prebuilt hex
  (peripheral first). SDC sysbuild hexes are at `<build-dir>/<app>/zephyr/zephyr.hex`.
- **Capture:** start `tools/capture-tool.py <seconds> /dev/cu.<cen>:<label>-cen /dev/cu.<per>:<label>-per`,
  wait for both ports to open, then reset peripheral and central. It asserts DTR and writes timestamped
  logs to `$CAP_OUTDIR` or `./captures/`. Do not start a timed capture after the reset/FSU transition.
- **Throughput** = receiver-delivered goodput: peripheral prints `SINK rx: … cum_total=<bytes>`
  (CoC) or `P t=<s>s rxkBps=<n>` (GATT); take the steady-window slope / median. Duplex aggregate
  = central `exkBps` (echo received) + peripheral `rxkBps` (blast received).
- **Analysis (turnkey):** `python3 tools/analyze.py <captured-log> [...]` — auto-detects the line format
  and prints the headline number (CoC/GATT throughput from the cum slope, latency mean/max/>30ms,
  the GATT RTT-vs-offered ramp) + the sanity flags (interval, PHY, DLE, `fsu=`). Steady window drops
  the first 25%; this generic trimming is **not** sufficient for a late SDC FSU onset. For on/off
  claims, use the matched-config/held gates and a post-onset window. See [`analyze.py`](tools/analyze.py).
- **Archived 84-cell sweep:** `debug-evidence/systematic-sweep-20260814/{sweep_build.sh,campaign.sh}`
  document the original campaign, **not a runnable current reproduction path**. They hard-code
  `/tmp`/author-specific app and board paths, capture after flashing rather than from reset,
  use historical unmatched FSU arms, and can continue after failed operations. Do not re-run them
  as a current benchmark; use the corrected per-result recipes and gates above.

## Other measurements
- **Latency (RTT vs interval):** z54-lat-central in ping-pong mode (echo peripheral); RTT is
  app-API→callback, reported per second.
- **Latency under load:** z54-lat-central `CONFIG_APP_LOAD_RAMP=y` (+ `CONFIG_APP_LOAD_POLITE=y`
  for the completion-paced arm); configs in
  [`debug-evidence/latency-under-load-20260813/app-configs/`](./debug-evidence/latency-under-load-20260813/app-configs/).
- **Soak:** the paced load-ramp build, left running; see
  `debug-evidence/latency-under-load-20260813/soak-under-load-20260815/`.
- **FSU on-air verification:** `tools/observer-smoke.sh` (see *On-air FSU observer* below);
  [`apps/nrf52/pca10040-radio-observer/`](apps/nrf52/pca10040-radio-observer/)
  + [`apps/misc/q2-central-2m/`](apps/misc/q2-central-2m/) (2M; the 1M endpoints in
  [`apps/misc/q2-central/`](apps/misc/q2-central/) are historical, see the known limitation below); see
  [`debug-evidence/fsu-q3a-20260812/`](debug-evidence/fsu-q3a-20260812/RESULTS.md) and
  [`debug-evidence/q3-2m-accept-20260906/`](debug-evidence/q3-2m-accept-20260906/RESULTS.md).
- **Watchdog / physical STOP:** independent actuator watchdog + logic analyzer; see
  [`safety/STOP-MEASUREMENT.md`](safety/STOP-MEASUREMENT.md) (design, results, wiring) and the
  `debug-evidence/wdt-stop-run*-20260805.sr` logic-analyzer captures.

## On-air FSU observer (nRF52 sniffer)
The observer ([`apps/nrf52/pca10040-radio-observer`](apps/nrf52/pca10040-radio-observer/README.md))
listens passively to the nRF54 pair's connection and timestamps every packet, so the inter-frame gap
is measured on air. A run connects the pair, requests FSU partway through a 30 s capture, and compares
the gap before and after against the peripheral's own on-chip timer.

**Rig:** 1× nRF52 DK (PCA10040) + 2× nRF54L15-DK, the observer placed roughly equidistant from both
nRF54 boards. Endpoint TX power is fixed in firmware (central +1 dBm, peripheral +8 dBm); those values
balanced received power at the observer *on this bench* and are rig-specific (see
[`apps/misc/q2-central-2m/README.md`](apps/misc/q2-central-2m/README.md)).

**Smoke test (any rig, ~10 min):** with the patched Zephyr tree (`ZEPHYR_BASE` set, `west` on PATH),
`nrfutil` and pyserial:
```
tools/observer-smoke.sh 2m
```
It detects the boards (override with `OBS_ID/OBS_TTY CEN_ID/CEN_TTY PER_ID/PER_TTY`), builds the
observer and the two endpoint images, then runs `apps/misc/q2-central/q2_run_2m.py` in
`--smoke --q3` mode with the frozen calibration. **Pass = `METRICS-OK`:** the gap steps from
≈150.4 µs to ≈52.4 µs, a step of 1568 ticks ±4, with on-air and on-chip agreeing within 8 µs and
per-phase retention ≥95%. The verdict prints **QUARANTINED** by design:
`--smoke` can never produce an accepted cell. A retention shortfall (<95%) is the known single-antenna
limit (about half of 2M reps on this bench on 2026-10-02/03): the wrapper retries it with a fresh rep,
up to `ATTEMPTS` (default 3), and keeps every attempt. If all attempts fall short, reposition the
observer; never relax the gate. Captures go to `observer-captures/`.

**1M is not re-runnable with current tooling.** The frozen 1M calibration records the hash of the
`analyze_q2.py` it was made with, and that file changed in the 2M port, so the analyzer rejects it
("calibration made by different tools"). The 1M runner also still uses pre-reorganisation relative
paths. Neither file can be edited without breaking re-verification of the accepted 2M result, so a
1M re-run needs a fresh calibration campaign (`combine_calib.py`). The accepted 1M result stands on
its archived record (`debug-evidence/fsu-q3a-20260812/`).

**Accepted cells** follow [`Q3-2M-ACCEPTANCE-PROTOCOL.md`](apps/misc/q2-central/Q3-2M-ACCEPTANCE-PROTOCOL.md):
the same runner without `--smoke` on a clean committed tree. Mid-step cells use the peripheral's
`fsu.conf` (auto-update off). The ABBA confirmation uses `fsu-au.conf`, the `f150` and `f52` central
arms in the order f150/f52/f52/f150 with `--q3-steady --abba-campaign-id <id> --abba-seq 0..3`, then
`combine_abba_2m.py`. A new rig also needs its own calibration controls (`combine_calib_2m.py`) before
its baseline can be trusted.

**Re-verifying the archived 2M ABBA:** copy the evidence with
`python3 tools/scrub-paths.py unscrub-tree debug-evidence/q3-2m-accept-20260906 <tmp>`, then run
`apps/misc/q2-central/combine_abba_2m.py --out <tmp>/recheck.json` on the four
`<tmp>/confirmation/abba-*` cells in A1 B1 B2 A2 order (expect `ABBA-CONFIRMED ... 1567.75t`). The
combiner re-hashes archived firmware configs, so it must see the original bytes.

The analyzers and runners are hash-bound by the combiners' current-lineage gate: editing them, even a
print label, breaks re-verification of every accepted cell. That is why the 2M tooling still prints
some legacy 1M wording ("on-air step == 50us", "150-100 delta", "@1M bin(s)"); the computed 2M values
are correct.

## Gotchas that silently corrupt your data
| gotcha | why it bites | do this |
|---|---|---|
| **Serial capture needs DTR asserted** | the DK's J-Link CDC gates console output on DTR — a raw reader gets *silence* and looks like a dead link | use `tools/capture-tool.py` (it asserts DTR+RTS), don't roll your own |
| **`nrfutil device program` leaves the core halted** | the image is written but doesn't run until a reset — the board is silent and never advertises or connects, which looks like a dead link | follow every `nrfutil device program` with `nrfutil device reset --serial-number <SN>` (`west flash` resets automatically) |
| **FSU-off arms emit no completion event on the open controller** | requesting 150 µs on a link already at 150 µs is a no-op, so there's no `FSU: updated` (GATT) / `Q3FSU-DONE` (CoC) line; a harness that requires one rejects every FSU-off rep | time the FSU-off window from the `FSU: request [150..150] rc=0` / `Q3FSU-REQ … min=150 max=150 rc=0` line (SDC does log `spacing=150`) |
| **SDC hex is at `<build>/<app>/zephyr/zephyr.hex`** | it's a sysbuild; flashing `<build>/zephyr/zephyr.hex` gives you the *open* image → you measure open while labeling it SDC | flash the sysbuild path, or `west flash -d <sdc-build-dir>` |
| **FSU must be configured on BOTH ends** | if only the central requests FSU, it negotiates but the peripheral never applies the shorter gap → a **false "FSU gives 0%"** | build the sink/echo with its FSU overlay (`open-fsu.conf`/`fsu-open.conf` set `CTLR_EVENT_IFS_LOW_LAT_US=52`; SDC sinks use `SDC_ENABLE_LOWEST_FRAME_SPACE=y` via `sdc-fsu.conf`/`sdc.conf`) and confirm it in the resolved `.config` |
| **Skip the warm-up window** | first few seconds are ramp/DLE/PHY negotiation (and FSU lands at ~6 s open / ~16 s SDC); including them understates throughput | `analyze.py` drops the first quarter, which is a generic summary only; for FSU on/off claims, take the window strictly after the FSU-onset timestamp |
| **Peripheral must extend data length (DLE)** | a peripheral that never calls `bt_conn_le_data_len_update` stays at 27-octet PDUs and throttles the reverse direction badly | the apps do this; if you fork them, keep it |
| **Flash peripheral first, then central** | the central connects on boot; if it boots before the peripheral is advertising it won't connect | order matters in the run scripts |
| **Stale latched state across back-to-back runs** | the sink latches `fsu_spacing` (and connection/credit state) and does NOT clear it on reconnect. An FSU-**off** run then reports a stale `fsu=52` → FSU state unverifiable and the result may reflect the *previous* config. | **Open capture first, then reset both boards into it** before every rep. Never chain runs without a fresh reset. A stale tag looks exactly like a valid reading. |
| **Un-pinned operating point** | comparing numbers taken at different interval / SDU / distance manufactures false effects — this is literally how the "CoC RF-collapse" artifact was created (a 50 ms binary vs a 15 ms baseline) | log `interval=…`, `spacing=…`, `PHY tx=2`, `DLE …251`, **and board distance** on every capture; only compare like-for-like |
| **`west flash -d /tmp …` fails "no CMake cache"** | `/tmp` isn't a build dir → flash no-ops and you measure the OLD firmware still on the board | use `nrfutil device program --firmware <hex> --serial-number <SN>` for a standalone prebuilt, or `west flash -d <actual-build-dir>` for a build; verify the boot image |
| **Peripheral pulls the connection interval to its own PREF (interval drift)** | the central sets 15 ms at connect, but the peripheral's auto-param-update requests its `PREF_MIN/MAX_INT` (e.g. 40 units = 50 ms) a second or two later → the interval silently drifts and latency/RTT jumps (bit us: idle RTT 24.8 ms → 80 ms mid-run). Throughput cells hide it; latency cells expose it. | pin the interval: set the peripheral `PREF_MIN/MAX_INT` to match the central AND `CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`. Verify the `GATE conn interval=` stays put and RTT is flat across the whole capture. |
| **Requesting FSU before PHY=2M + full DLE → `-EACCES` (rc=-13), FSU silently never arms** | if the app requests FSU at L2CAP-connect time but the link is still 1M / DLE tx_max=27 (PHY/DLE complete slightly later), the controller rejects the FSU request with -13 and the sink shows `fsu=0` forever — FSU-off without any error in the throughput/RTT numbers. Bit us in the coclat rig: L2CAP was opened on the *first* DLE update (tx=27) instead of the full one. | **request FSU only after PHY=2M AND DLE tx_max≥251** — e.g. gate `bt_l2cap_chan_connect` (and thus the FSU request) on `le_data_len_updated` where `tx_max_len>=251`. Always confirm `Q3FSU-REQ rc=0` → `spacing=52` / sink `fsu=52` before trusting an FSU-on run. |
| **Kconfig silently DROPS a symbol with an unmet dependency** | setting `CONFIG_BT_X=y` whose dependency chain isn't fully enabled → Kconfig emits only a *warning* (`assigned 'y' but got 'n' … unsatisfied dependencies`), the **build still succeeds**, and the feature runs at its default (off) with **no runtime error**. Bit us on EATT: `BT_EATT`→`BT_L2CAP_ECRED`→`BT_L2CAP_DYNAMIC_CHANNEL` (off) → EATT compiled out, app ran as plain GATT with **0 bearers** while we thought EATT was on. Same class as the stale-tag gotcha: the wrong config looks like a valid run. | **enable the full dependency chain explicitly**, then **grep the effective `build/zephyr/.config`** (not your `prj.conf`) for every symbol you set — `CONFIG_BT_EATT=y` etc. — and **verify at runtime** (`bt_eatt_count(conn)≥N`, `fsu=52`, `GATE conn interval=`). A dropped feature is invisible unless you check both. |
| **EATT bearer MTU is smaller than you set — it reserves 6 bytes** | two traps, both silent: (1) EATT bearers default to a small `CONFIG_BT_L2CAP_TX_MTU` (~65) unless you raise it; (2) **even after raising it, the EATT bearer RX MTU = `BT_BUF_ACL_RX_SIZE − 6`** (EATT's per-frame reservation, vs −4 for a fixed bearer). So with `BUF_ACL_RX_SIZE=251` the bearer MTU is **245**, and a 244-byte write (= 247-byte ATT PDU) is rejected: `<err> bt_l2cap: attempt to send 247 bytes on 245 MTU`. That `<err>` fires **per operation** → a flood that drowns telemetry (`--- N messages dropped ---`, lost `PCTL`/`RPT` lines) **and** perturbs timing, so the run is garbage (our EATT A/B: 30 k error lines, ramp markers dropped, bogus RTT). Cost us two rebuild+run iterations. | size your **max ATT PDU (payload + 3) ≤ `BT_BUF_ACL_RX_SIZE − 6`** — e.g. with `BUF_ACL_RX_SIZE=251` keep the payload ≤ 242 (we use 240). Set `CONFIG_BT_L2CAP_TX_MTU=247` too. Then **grep the capture for `<err> bt_l2cap`** and discard the run if any appear — an `<err>` repeated per-op is never benign, and it silently invalidates every number in that capture. |

## Sanity checks — confirm you're measuring the right thing
The campaign tools apply these automatically per rep via `tools/link_gates.py` (since 2026-10-06): PHY 2M both ways,
data length 251 (both ways for duplex, TX for a one-way sender), the interval held from connect to the end of the
capture (the CoC centrals now log every parameter update as `GATE conn: interval=`), and for CoC the credit window
(the peripheral's initial TX credits = 64, i.e. the central sent its window in the connection request; neither side's
running balance above 64, i.e. no leak). For other rigs, grep the captured logs before trusting a number:
- **2M PHY negotiated:** central log shows `PHY … tx=2 rx=2` (or `PHY tx=2`). If it's 1M, throughput ≈ half.
- **Data length extended:** `DLE … tx_max=251` (or `rx_max=251`). If 27, you're fragmenting.
- **FSU actually applied (FSU arms only) — capture from BOOT (the line prints once at setup):**
  - **CoC / CoC-lat / duplex:** the *sink* prints it — `SINK rx … fsu=52` and `Q3FSU-DONE role=P … spacing=52`. `fsu=0`/`spacing=150` on an FSU-on run = it didn't apply (usually the sink built without `open-fsu.conf`) → see the "both ends" gotcha.
  - **GATT (`z54-lat` / EATT):** the responder emits **no** FSU telemetry — verify on the *central*'s `FSU: updated … spacing=52 us` line. The GATT responder's applied spacing is not independently observable from its own log, so a clean central line + the both-ends config check is the confirmation.
- **Stable measurement window:** no unexplained mid-run step-down, interval drift, or dropped error
  lines. Verify the onset and take a post-FSU window; do not assume the first quarter is always enough.

Do **not** use an expected throughput curve, peak interval, or presumed short-interval FSU null as
an acceptance gate.
Accept/reject by operating point, matched configs, held-FSU evidence, and capture integrity; compare
only like-for-like arms. See the [ledger](docs/EVIDENCE-INDEX.md) for result-specific status.

## Troubleshooting
| symptom | likely cause |
|---|---|
| `CONFIG_BT_CTLR_FRAME_SPACE_UPDATE` undefined at build | unpatched Zephyr — apply the `fsu-m0` series (prereq #1) |
| `BT_LL_SOFTDEVICE undefined` on an SDC build | Zephyr env poisoning — build in the `unset ZEPHYR_BASE` NCS subshell (prereq #3) |
| FSU shows ~0% gain | first check the responder's resolved config, auto-update setting, held-throughput trace, and post-onset window; a latched `spacing=52` token alone is insufficient |
| SDC numbers ≈ open numbers everywhere | you flashed the open hex for the "SDC" run — wrong sysbuild hex path |
| throughput ~half expected | link fell back to 1M PHY (no `PHY tx=2`) |
| no console output after a power cycle | reader isn't asserting DTR — use `tools/capture-tool.py` |
| link won't connect | central booted before peripheral advertised — flash/boot peripheral first |

## Other result-specific recipes and status
The degraded-RF/range campaign is **partial**, not absent. The CoC-duplex reconnect wedge is resolved
(a Zephyr host bug on 0 initial credits, avoided in the apps; `coc-credit-fixes-20261006`). For their current status, use
[EVIDENCE-INDEX](docs/EVIDENCE-INDEX.md),
[LESSONS](docs/LESSONS.md), and the [CoC overview](docs/overviews/coc-technical-overview.md).
For on-air FSU verification with the nRF52 observer, see *On-air FSU observer* above.
Independent professional-analyzer qualification is separate from this observer-based acceptance.

## Provenance
Per-campaign evidence (raw logs/configs where retained, firmware hashes, methodology) lives
under [`debug-evidence/`](./debug-evidence/); the 2026-08-25 on-bench re-tests (matched-config
reproduce, CoC-vs-GATT range, FSU-vs-interval, CoC-duplex) are in
[`debug-evidence/coc-vs-gatt-rangetest-20260825/`](./debug-evidence/coc-vs-gatt-rangetest-20260825/)
(FINDINGS + raw logs). Absolute KB/s are RF-environment-specific; deltas also require a matched
operating point and repeated runs. **Log interval + SDU + `spacing` + distance
on every capture** (see Gotchas) — the un-pinned-operating-point failure is what created the
retracted "CoC RF-collapse."
