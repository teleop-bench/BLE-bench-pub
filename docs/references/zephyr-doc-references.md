# Zephyr documentation references (mapped to our benchmark)

Curated Zephyr doc links for this benchmark, each tied to the finding, knob, or open question it
bears on. Built from an in-depth read of the host / controller / throughput-sample / ISO-EATT-coex
doc areas (2026-08-26), cross-checked against our local `v4.4.1` + `fsu-m0` tree.

**Citation hygiene:** `/latest/kconfig.html` is a **client-side search box** (no per-option static
pages — `kconfig.html#CONFIG_X` does NOT deep-link). Cite a Kconfig option by its **exact `CONFIG_`
name** (durable, greppable) and, if a stable URL is needed for publication, a **version-pinned**
static page (`/<ver>/reference/kconfig/CONFIG_*.html`, e.g. 2.7.5 / 3.0.0). Behavior below is quoted
from our actual tree where the prose docs are silent (they usually are — see Doc Gaps).

## Core prose pages (verified URLs — the docs moved to `/services/connectivity/bluetooth/`)
- **Bluetooth overview** — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/index.html
- **Stack architecture** (host↔ctlr split, combined vs HCI, RX-thread callbacks must not block) — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/bluetooth-arch.html
- **LE Host** (buffers, HCI flow control / Number-of-Completed-Packets) — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/bluetooth-le-host.html
- **LE Controller architecture** (ULL/LLL, Ticker, pre-emption, execution priorities) — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/bluetooth-ctlr-arch.html
- **Supported features / maturity table** (what's `[EXPERIMENTAL]` on the open controller) — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/features.html
- **Application development / flow control** — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/bluetooth-dev.html
- **L2CAP API** (CoC, credit-based flow control, seg_recv) — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/api/l2cap.html
- **`bt_l2cap_chan_ops`** (seg_recv / sent / alloc_seg semantics) — https://docs.zephyrproject.org/apidoc/latest/structbt__l2cap__chan__ops.html
- **GATT API** (notify / write-without-response / notify-multiple) — https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/api/gatt.html
- **net_buf** (pool model, K_FOREVER alloc, ref-counting) — https://docs.zephyrproject.org/latest/services/net_buf/index.html
- **Kernel scheduling / threads / workqueue / FIFO** — https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html · .../threads/workqueue.html · .../data_passing/fifos.html
- **nRF54L15-DK board** (thin — no per-feature validation notes) — https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15dk/doc/index.html
- **Kconfig search** — https://docs.zephyrproject.org/latest/kconfig.html

## Samples / benchmarks (there is NO mainline one-way GATT/CoC throughput sample)
- **`iso_connected_benchmark`** — the only official BLE "benchmark"; reports **packet loss** (rolling 1000 + cumulative), not KB/s — https://docs.zephyrproject.org/latest/samples/bluetooth/iso_connected_benchmark/README.html
- **`mtu_update`** — the only sample that *discusses* throughput, qualitatively (MTU 23→247) — https://docs.zephyrproject.org/latest/samples/bluetooth/mtu_update/README.html
- **`l2cap_coc_acceptor` / `_initiator`** — CoC setup demo, no throughput/credit reporting — https://docs.zephyrproject.org/latest/samples/bluetooth/l2cap_coc_acceptor/README.html
- **`iso_broadcast` / `central_iso`** — fleet stop-beacon primitives (see roadmap) — https://docs.zephyrproject.org/latest/samples/bluetooth/iso_broadcast/README.html · .../central_iso/README.html
- *(The KB/s "Bluetooth: Throughput" sample readers may look for is **Nordic nRF Connect SDK**, not upstream Zephyr.)*

## Knobs we use → exact name + where defined → our finding
| `CONFIG_` name | defined in | our finding |
|----------------|-----------|-------------|
| `BT_L2CAP_SEG_RECV` | host/Kconfig.l2cap:61 `[EXPERIMENTAL]` | app-controlled steady RX credit pool (vs per-SDU-boundary regrant) → **+28%** multi-segment |
| `BT_BUF_ACL_TX_COUNT` (dflt 3) | common/Kconfig:34 | host→ctlr **NoCP window**; sizes `conn_tx[]` completion array + pulls `L2CAP_TX_BUF_COUNT`/`ATT_TX_COUNT`. Deep (64) doesn't lift the 15 ms peak (airtime), helps at 50 ms. **Shared across connections/directions** (duplex confounder) |
| `BT_L2CAP_TX_BUF_COUNT` (dflt =ACL_TX_COUNT) | host/Kconfig.l2cap:9 | outgoing **PDU pool** (distinct from the NoCP window); the depth that matters at 50 ms |
| `BT_CONN_TX_MAX` | host/Kconfig:379 **`[DEPRECATED]`** | ⚠️ **inert in v4.4.1 — referenced nowhere in code.** Our `-D...=64` was a no-op; drop from any tuning claim. Only `BT_BUF_ACL_TX_COUNT` matters |
| `BT_BUF_ACL_TX_SIZE` (dflt 27) | common/Kconfig:9 | must be **≥251** or the host fragments every DLE-251 PDU onto `BT_L2CAP_TX_FRAG_COUNT`. *(Checked 2026-08-26: our builds already =251 → NOT the DLE=251 stall cause; hypothesis falsified.)* |
| `BT_L2CAP_TX_FRAG_COUNT` (dflt 2) | host/Kconfig.l2cap:16 | fragment buffers; help warns too-few can **deadlock** — not exercised here since TX_SIZE≥DLE |
| `BT_BUF_ACL_RX_COUNT_EXTRA` | common/Kconfig | sets `L2CAP_LE_MAX_CREDITS = BT_BUF_ACL_RX_COUNT-1` — RX-side credit ceiling. *(Not the stall cause: signature is txcred frozen-high, not starved.)* |
| `BT_CTLR_DATA_LENGTH_MAX` (27–251) | controller/Kconfig:609 | DLE 251 → a 251 B PDU ≈ **1.05 ms of 2M airtime** (the ceiling arithmetic); `BT_BUF_ACL_RX_SIZE` silently clamps it |
| `BT_CTLR_PHY_2M` | controller/Kconfig:714 | 2M halves airtime/PDU → ~2× PDU/event vs 1M (our 4.87/9.64/16.30 line is 2M) |
| `BT_CTLR_FORCE_MD_COUNT` / `_AUTO` | Kconfig.ll_sw_split:1168 | More-Data hold-open; **arms only when `trx_cnt ≥ BT_BUF_ACL_TX_COUNT-1`** (deep pool → never arms; the nRF52 throttle) |
| `BT_CTLR_ADVANCED_FEATURES` | Kconfig.ll_sw_split:354 | **visibility switch only** — no functional effect; it just unhides FORCE_MD etc. (silent-drop gotcha) |
| `BT_CTLR_SLOT_RESERVATION_UPDATE` | Kconfig.ll_sw_split:884 | **sizes** event reservation after DLE/PHY to avoid overlap; does **not** extend a lone event |
| `BT_GAP_AUTO_UPDATE_CONN_PARAMS` | host/Kconfig | =n on the sink so the central's pinned interval holds (Stage 1 interval-pin) |
| `BT_TESTING` | host/Kconfig | enables the `bt_test_l2cap_data_pull_spy` path; our gated `l2cap_pull_pdus` host-handed counter |
| `BT_USER_DATA_LEN_UPDATE` | host/Kconfig | app-initiated DLE — the peripheral fix that killed the ~43 KB/s uplink "asymmetry" |

## Controller mechanism — source-level (undocumented in prose; `ll_sw/nordic/lll/lll_conn.c`)
- **Event length = More-Data continuation + ticker preemption** (`:881`): a lone connection's event
  airs PDUs back-to-back (each side's MD bit) until the queue drains or the interval boundary — **there
  is NO SoftDevice-style event-length *extension* in the open controller.** This IS why our ceiling is
  airtime and scales with interval. (`SLOT_RESERVATION_UPDATE`/`*_RESERVE_MAX` only matter multi-connection.)
- **FORCE_MD arm threshold** = `BT_BUF_ACL_TX_COUNT-1` (`:442`); our custom `APP_FORCE_MD_ARM_AT`
  (`:439`) decouples it — an un-swept throughput lever.
- **Peripheral early-close TODO** (`:870`): the peripheral event force-closes one drift-window early;
  the source comment flags removing it "to improve throughput … under high throughput" — a standing,
  un-exploited **uplink** opportunity.
- **`FSU_EVENTFILL_DIAG` self-throttle**: the occupancy diag itself costs ~20% CoC throughput —
  never publish a throughput number with it on (we don't).

## Untried levers surfaced (candidate next work)
- **`BT_EATT`** (+`BT_EATT_MAX` dflt 3, `[EXPERIMENTAL]` host; needs peer support + encryption) — a
  per-signal ATT bearer for the stop-signal → the canonical fix for the latency-under-load head-of-line
  tail, GATT-native (no 2nd-CoC credit-reconnect fragility). **Third arm** to separate-CoC vs completion-pacing.
- **`BT_GATT_NOTIFY_MULTIPLE`** — coalesce small notifications into one PDU (amortize headers).
- **`BT_CONN_TX_NOTIFY_WQ`** (`[EXPERIMENTAL]`) — run completion callbacks off the system workqueue;
  A/B for the latency-under-load tail.
- **`BT_SUBRATING`** (non-experimental since v4.2.0) — hold a tiny underlying interval for RTT while
  subrating hard when idle → the **idle-power lever** for an always-on backup (no param-update round-trip).
- **ISO BIS/CIS** (`BT_CTLR_ADV_ISO`/`SYNC_ISO`/`*_ISO`, all `[EXPERIMENTAL]` on the open controller) —
  one-to-many **stop-beacon** with tunable RTN / max-transport-latency (bounded-latency, no backpressure).
- **`alloc_seg`** — app-supplied segment buffers to cut RX buffer pressure at deep credits.

## Coexistence boundary (load-bearing for the Wi-Fi-failover use case)
- **Open stack:** only a reactive 1-wire GPIO grant/abort — DT `radio-gpio-coex` (renamed from
  `gpio-radio-coex` in **v4.4.0**; [PR #51419](https://github.com/zephyrproject-rtos/zephyr/pull/51419)).
  **BLE always yields**; no PTA/priority/prediction. Failure mode: Wi-Fi hung but still asserting grant
  could starve the backup exactly when it must take over.
- **Real Wi-Fi/nRF70 coex** = **MPSL CX + the proprietary SoftDevice Controller** — [Nordic Wi-Fi coex](https://nrfconnectdocs.nordicsemi.com/ncs/3.0.2/nrf/app_dev/device_guides/wifi_coex.html). Out of scope for the open benchmark; state the boundary explicitly.
- **DEPLOYMENT NOTE:** this project's robots run **Wi-Fi on 5 GHz
  only**, BLE on 2.4 GHz → this **substantially removes direct co-channel overlap** (the main thing
  SDC MPSL-CX is for), so coex is a much weaker reason to prefer SDC. **Not eliminated**, though:
  same-device front-end coupling / desense / harmonics-IMD / shared-antenna / radio-scheduling
  interactions can still occur and **need validation** (AFH can't fix a co-located desense). Separately,
  2.4 GHz *airwave* congestion from other emitters is handled by AFH/retransmit/timing-budget, not coex HW.

## Doc gaps = contribution candidates (measured by us, undocumented upstream)
> Drafted as submittable upstream doc/Kconfig patches in [`zephyr-doc-contributions.md`](./zephyr-doc-contributions.md).
1. **No KB/s ceiling / PDU-per-event / tIFS-airtime model anywhere** — the only trace is
   `BT_CTLR_RX_BUFFERS` help ("18 packets @ 1 B, 7.5 ms, 2M", RX-sizing framing). Our large-payload
   line (4.87/9.64/16.30 PDU/ev @ 251 B / 7.5·15·25 ms) is original.
2. **No mainline one-way GATT/CoC throughput sample** (only ISO packet-loss).
3. **No buffer-sizing rule** tying `BT_L2CAP_TX_BUF_COUNT`/`BT_BUF_ACL_TX_COUNT` to interval×PHY airtime
   (our 50 ms buffer effect is that missing rule); **no K_FOREVER-exhaustion warning** in net_buf.
4. **seg_recv documented as API only** — our +28% quantifies the per-SDU-regrant cost it removes.
5. **NoCP-window × airtime-capacity interaction** — undocumented; our central result. ("NoCP not the
   limiter" holds only *above the airtime knee*; at the default depth-3 window it would bind.)
6. **Duplex / FSU / latency-under-load / GATT≈CoC parity + EATT duplex benefit** — entirely absent.
