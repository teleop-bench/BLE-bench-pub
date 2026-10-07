# Zephyr 2.7 → 4.4.1 — what changes for this benchmark

Our benchmark was on **Zephyr 2.7.1, open `ll_sw_split` controller, no SoftDevice**.
This records what newer *open* Zephyr (up to **v4.4.1**, latest as of 2026-07) changes,
from a changelog + issue/PR review. Sources: Zephyr release notes 3.0–4.4 + migration
guides, and the GitHub issues cited.

## ✅ MEASURED — re-benchmark on Zephyr 4.4.1 (2026-07-30)

Ported both firmwares to upstream **Zephyr 4.4.1 / west / Zephyr SDK**, open
`ll_sw_split` controller, and re-ran the throughput test. Same hardware
(nRF52840 dongle = central sender, nRF52832 DK = peripheral sink), same link
(negotiated on-air: `interval=6`→7.5 ms, `PHY tx=2 rx=2`→2M, `rx_max=251`→DLE-251).

| Payload | Zephyr 2.7.1 | **Zephyr 4.4.1 + `seg_recv`** | Δ |
|---|---|---|---|
| **244-byte SDU** (single-packet) | ~150 KB/s | **~151 KB/s** (147–154, 16 samples) | **~0 — wall unmoved** |
| **480-byte SDU** (multi-packet) | ~117 KB/s | **~150 KB/s** (148–154, 10+ samples) | **+28% (~+33 KB/s)** |

*Single cross-version comparison on one rig, not counterbalanced; the 4.4.1 points have fewer runs
than the deeply characterized 2.7 baseline, and session-to-session scatter in this project was ±10–20%.*

Both SDU sizes measured on 4.4.1 (not assumed). The 244 B point is the control:
it sat at the air-time wall on 2.7 and stays there on 4.4.1 (~150→~151). So the
4.4.1 improvement is precisely **"480 B caught up to 244 B"** — the multi-packet
refill-starve penalty was removed, the ~150 KB/s wall itself did **not** move.
Rules out both alternatives (everything-shifted-up; 244 B regression). No headroom
left above ~150 at any SDU size on the open stack.

**Finding — `seg_recv` is the lever, and it partially falsifies the old model.**
On 2.7 we called the 480 B ceiling "refill-starve = structural air-time." The
re-benchmark shows the 3.4+ **hardcoded RX-credit-of-1** was a *real, removable*
limiter: giving explicit credits via `seg_recv` + `bt_l2cap_chan_give_credits()`
(credit window 20, replenish 1 per segment) lifts the multi-packet 480 B case to
the **same ~150 KB/s air-time wall** that on 2.7 only the 244 B single-packet case
reached. The ~150 KB/s wall itself is unchanged — that part of the old model holds.
Port cost was as estimated (~low-single-digit eng-days); build clean, no rewrite.

Firmware: `z44-central/` (dongle sender) + `z44-dk-sink/` (DK seg_recv sink).

## Our specific issues, on 4.4.1

| Issue | Status on 4.4 | Action |
|---|---|---|
| **PHY-race bimodality** (stuck-1M; [#31473](https://github.com/zephyrproject-rtos/zephyr/issues/31473) / [#41788](https://github.com/zephyrproject-rtos/zephyr/issues/41788)) | **Fixed — best-improved.** LLCP fully rewritten (new impl default since 3.2, PR-era); both issues closed. **4.3 added role-specific auto-PHY** (`CONFIG_BT_AUTO_PHY_{CENTRAL,PERIPHERAL}_{NONE,1M,2M,CODED}`) to pin one side and remove the collision outright. | **Our verify-then-sequence app hack is obviated** — use the config. |
| **CoC RX credits** ([#69975](https://github.com/zephyrproject-rtos/zephyr/issues/69975)) | 3.4 hardcoded RX credits to **1** (8× drop) — the **default from 3.4 on**. Fixed via the **`seg_recv` API** (`CONFIG_BT_L2CAP_SEG_RECV` + `bt_l2cap_chan_give_credits()`; stable 3.4→4.4). | **Must adopt `seg_recv` or a naive port *loses* RX throughput.** |
| ~~**#46073 peripheral→central TX-context wedge**~~ **← CITATION WAS WRONG.** #46073 is the *IPSP (IPv6-over-BLE) sample* bug, closed-as-stale 2022 — not our L2CAP CoC case. Correct lineage: **#76737** (L2CAP TX frag state not cleared on disconnect; fixed+backported to v3.7), #76738/#43440/#27434. | **RE-TESTED 2026-07-30 (see [UPLINK-RECONNECT-FINDINGS.md](UPLINK-RECONNECT-FINDINGS.md)).** Sustained uplink HEALED; clean link-drop recovers; **central power-cycle still wedges** the reconnected CoC channel (zero `.sent` completions). Completion-paced sender is the correct pattern but insufficient for the power-cycle case. | Uplink measured: initially ~43 vs ~150 downlink, but the gap was a **firmware bug** (peripheral never requested DLE → TX stuck at 27 octets). Fixed (peripheral `bt_conn_le_data_len_update`) → **uplink ~152 ≈ downlink, NO asymmetry** (CoC & GATT both ~150-155 both ways). |
| **"No connection-event-length knob in the open controller"** | **Partly outdated.** 3.3 (PR [#52012](https://github.com/zephyrproject-rtos/zephyr/pull/52012)) added `CONFIG_BT_CTLR_SLOT_RESERVATION_UPDATE` — recalculates the event slot *after* DLE/PHY, so a 2M+DLE link can reserve a **longer event** (more packets/event). Still *reservation sizing*, **not** the SoftDevice's opportunistic *extension*, so **~160–170 KB/s stays the open-stack wall**. | **Enable it, re-measure packets/event; keep the "no opportunistic CEL" point.** |

## Expected vs measured outcome

- **Predicted "modestly higher, not transformed" → confirmed, and better than "modest."** Measured **+28%** (117→150 KB/s) at 480 B — the average/worst-case *did* rise more than best-case (244 B single-packet was already at the wall). The refill-limit did **not** persist as pure air-time: `seg_recv` credits removed it. `SLOT_RESERVATION_UPDATE` effect not separately isolated (150 ≈ the single-packet air-time wall, so any event-growth headroom is masked at this payload).
- **~150–170 KB/s wall confirmed unchanged** — 480 B now sits at ~150, the same wall 244 B hit on 2.7. Air-time/spec ceiling, not a Zephyr limit; nothing in 3.0–4.4 raises it for the open controller (no opportunistic event-length extension without the proprietary SoftDevice Controller — out of scope, same as ESB).

## New open-controller primitives (roadmap)

- **ISO / BIS matured in the open controller** (BIG 3.0 → full ISO TX 3.1 → encryption/CIS 3.3 → advanced params 3.5). The **right primitive for the fleet stop-beacon** (connectionless one-to-many) — and *open*, no SoftDevice.
- **Connection Subrating non-experimental (4.2)** — fast base interval for stop-latency, subrate idle traffic. Useful for the safety link's power/latency tradeoff.
- **Coded PHY** unchanged in this window (predates 3.0); still needs Coded-capable silicon (our nRF52832 peripheral can't). 4.3 role-auto-PHY adds a `CODED` option per role.

## Firmware-port scope (2.7 → 4.4)

Moderate, **not a rewrite** (~low-single-digit engineer-days for the link layer + re-characterization):
- **Include-path move to `<zephyr/...>`** (3.1; shim removed by 4.0) — scripted via `scripts/utils/migrate_includes.py`.
- **L2CAP `accept()` signature** (3.5) — adds a `struct bt_l2cap_server *server` param.
- **L2CAP state enum rename** (3.1) — `BT_L2CAP_CONNECT` → `BT_L2CAP_CONNECTING`.
- **TX buffer semantics** (3.7) — BT-TX thread + segment pools removed; re-derive `CONFIG_BT_L2CAP_TX_*` / ACL-TX pool sizing, don't copy 2.7 values.
- **CoC RX path** → re-implement on `seg_recv` + `give_credits`.
- **PHY setup** → 4.3 role-specific auto-PHY (drop the app-side sequencing hack).
- No change found to `bt_l2cap_chan_send` / `NET_BUF_POOL_FIXED_DEFINE` / `k_*` / `printk` (beyond the include prefix). 4.4 minor deprecations: `bt_conn_le_info.interval` → `interval_us`.

## Toolchain

Build with **upstream Zephyr v4.4.1 + `west` + Zephyr SDK**, open `ll_sw_split`
controller. No NCS / SoftDevice Controller in this nRF52 comparison (the later nRF54L15
benchmark adds an SDC arm). See [REPRODUCE.md](../../../REPRODUCE.md) for toolchain setup.

> **Honesty flags** (from the changelog review, unverified at source-diff level): exact
> release that removed legacy LLCP; that `FORCE_MD`/`BT_BUF_ACL_TX_COUNT` arming is
> byte-identical on 4.4; fix versions for #31473/#41788. Re-characterization on hardware
> was required (and was done — see the table above). The earlier #46073 citation was wrong; see
> [`UPLINK-RECONNECT-FINDINGS.md`](UPLINK-RECONNECT-FINDINGS.md) for the corrected lineage.
