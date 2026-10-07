# Prebuilt hexes — archival hardware smoke test (no build, no patching)

> **Archival only — do not use these to reproduce the current FSU or open-vs-SDC headlines.**
> These August 2026 builds (v4.4.1-based) are a no-toolchain hardware smoke test, driven by
> [`run-all.sh`](../run-all.sh). Known limits:
> - The GATT FSU-off centrals (`c-gatt-open-off-7p5`, `c-gatt-sdc-off-25`) were built **without the
>   FSU package**, so a GATT on/off delta from them is not a matched-arm comparison
>   ([LESSONS #2](../docs/LESSONS.md)).
> - The original CoC sinks (`p-coc-sink-open`, `p-coc-sink-sdc`) were built with GAP auto-update on:
>   their ~5 s connection-parameter update silently reverts FSU, and a CoC replay from them gave a
>   near-zero FSU gain that was later retracted ([LESSONS #1](../docs/LESSONS.md)). They are kept for the
>   record. `run-all.sh` now uses `p-coc-sink-open-noau` / `p-coc-sink-sdc-noau`, rebuilt 2026-10-02 from
>   the current `coc-sink` source with `CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` (open: same v4.4.1-15 tree
>   as the other images, banner `g1772af7b563e`; SDC: NCS v3.4.0).
> - All CoC sinks here return **one credit per received segment**, which costs exchanges with FSU at short
>   intervals (7.5 ms: 0% gain instead of +20%) and depresses SDC at 15 ms (FSU off ~141 vs ~156 KB/s with
>   batched returns; `debug-evidence/coc-credit-policy-20261006/`, `oneway-coc-batched-20261006/`). The
>   open 15 ms pair reproduces the README headline either way (batched: 155 → 186). For other cells build
>   the sink with `-DCONFIG_APP_CREDIT_BATCH=y`.
>
> For current results, build the matched arms in [REPRODUCE.md](../REPRODUCE.md) and check the
> [evidence index](../docs/EVIDENCE-INDEX.md).

**License:** the `*-sdc*` images contain Nordic's proprietary SoftDevice Controller and are under
Nordic's 5-Clause license (Nordic chips only, no reverse engineering), not Apache-2.0; all images
also contain BSD-3-Clause nrfx. See [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

Ready-to-flash firmware for the August benchmark configs, so you can exercise the rig by
**flashing + capturing** without assembling the patched Zephyr / NCS toolchain. `nrf54l15dk/nrf54l15/cpuapp`,
2M PHY. Built with the FSU event-fill diagnostic **off** (`CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=n`)
so throughput isn't throttled. Verify with `SHA256SUMS`.

## Pairs (flash peripheral first, then central)
| benchmark | central | peripheral |
|---|---|---|
| GATT one-way, open, FSU on/off @7.5 ms | `c-gatt-open-fsu-7p5` / `c-gatt-open-off-7p5` | `p-gatt-sink-open` |
| GATT one-way, SDC, FSU on/off @25 ms | `c-gatt-sdc-fsu-25` / `c-gatt-sdc-off-25` | `p-gatt-sink-sdc` |
| GATT duplex, open | `c-gatt-open-*` (same central) | `p-gatt-echo-open` |
| GATT duplex, SDC | `c-gatt-sdc-*` (same central) | `p-gatt-echo-sdc` |
| CoC one-way, open, FSU on/off @15 ms | `c-coc-open-fsu-15` / `c-coc-open-off-15` | `p-coc-sink-open-noau` (original: `p-coc-sink-open`) |
| CoC one-way, SDC, FSU on/off @15 ms | `c-coc-sdc-fsu-15` / `c-coc-sdc-off-15` | `p-coc-sink-sdc-noau` (original: `p-coc-sink-sdc`) |

## Flash + capture
```
nrfutil device program --firmware <periph>.hex  --serial-number <periph-board-id>
nrfutil device program --firmware <central>.hex --serial-number <central-board-id>
python3 tools/capture-tool.py 50 /dev/cu.<central>:run-cen /dev/cu.<periph>:run-per &
# once both ports are open, reset into the capture so the boot/FSU lines are recorded:
nrfutil device reset --serial-number <periph-board-id>
nrfutil device reset --serial-number <central-board-id>
```
(Linux: `/dev/ttyACM<n>`.) Or run [`run-all.sh`](../run-all.sh), which does all of this and
applies the gates.
Peripheral prints goodput (`SINK rx … cum_total=` for CoC, `P t=…s rxkBps=` for GATT); take
the steady-window slope/median. Sanity-check: `PHY tx=2`, `DLE … 251`, and (FSU arms) the
peripheral's `spacing=52` — see REPRODUCE.md "Sanity checks."

## Important
- **Only the August cells are prebuilt.** Other intervals: build via REPRODUCE.md.
- **Absolute KB/s depend on YOUR RF environment** (distance, interference). Even valid deltas
  require matched arms and held FSU (see the banner above); don't expect our exact numbers. L2CAP CoC results were
  especially sensitive to session and arrangement in our bench runs; whether RF specifically
  caused that variation remains unresolved — see `coc-technical-overview.md` §4.
