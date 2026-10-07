# nRF52 (Bluetooth 5) L2CAP CoC — earlier-hardware findings

Before the nRF54L15 (Bluetooth 6) benchmark, this project measured L2CAP Connection-Oriented
Channel throughput and robustness on nRF52 hardware with the open Zephyr controller
(`ll_sw_split`, no SoftDevice): an **nRF52832 DK** as peripheral and an **nRF52840 Dongle** as
central, 2M PHY, Data Length Extension, 7.5 ms interval. Three findings from that work are
published here because they matter beyond this rig. The current results are in the
[top-level README](../../../README.md).

## Findings

| Finding | Status | Write-up |
|---|---|---|
| **Reconnect wedge:** after the *central* reboots and reconnects, the peripheral's reconnected CoC channel produces zero TX completions; a clean link drop with the central kept alive recovered in all 4 tested cycles. A matching central-reboot reconnect wedge later appeared on nRF54L15 (the 25 ms duplex uplink stall). | Observed and characterized on both platforms; **candidate Zephyr bug, unfiled** — no minimal standalone reproduction yet. The closest upstream issue is [#76737](https://github.com/zephyrproject-rtos/zephyr/issues/76737) (an earlier #46073 citation was wrong). | [`UPLINK-RECONNECT-FINDINGS.md`](UPLINK-RECONNECT-FINDINGS.md) §2, [`zephyr-bug-report.md`](zephyr-bug-report.md) |
| **"3.5× uplink asymmetry" was a firmware bug:** uplink first measured ~43 vs downlink ~150 KB/s because the peripheral never called `bt_conn_le_data_len_update` and stayed at 27-byte PDUs. Fixed, uplink ≈ downlink (~152 vs ~150–155 KB/s). | Retracted claim; the lesson is a REPRODUCE gotcha. | [`UPLINK-RECONNECT-FINDINGS.md`](UPLINK-RECONNECT-FINDINGS.md) §1 |
| **Zephyr 2.7.1 → 4.4.1 with `seg_recv`:** 480-byte SDU throughput ~117 → ~150 KB/s (+28%); the 244-byte control stayed at ~150 → ~151. | Single cross-version comparison on one rig (not counterbalanced); the 4.4.1 point has fewer runs than the 2.7 baseline. | [`ZEPHYR-4x-REVISIT.md`](ZEPHYR-4x-REVISIT.md) |

## Firmware

- **Zephyr 2.7.1 baseline** (built with PlatformIO, `platform = nordicnrf52`, `framework = zephyr`):
  this directory (`src/`, `zephyr/`, `platformio.ini`) is the nRF52832 DK peripheral/sink;
  [`../nrf52840-l2cap-central/`](../nrf52840-l2cap-central/) is the nRF52840 Dongle central.
- **Zephyr 4.4.1** (built with `west`): [`../../z44/`](../../z44/) — `z44-central` + `z44-dk-sink`
  (the 4.4.1 throughput points), `z44-uplink-*` (uplink + reconnect), `z44-gatt*` (GATT
  comparison), `z44-repro-*` (the unfinished minimal reproduction).

Absolute KB/s are rig- and session-specific; compare only matched operating points (see
[REPRODUCE.md](../../../REPRODUCE.md)). Other nRF52-era working notes are not published.
