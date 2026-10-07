# BLE-bench — reproducible Bluetooth LE 5/6 throughput & latency benchmark

An open, reproducible benchmark of **Bluetooth LE application throughput and latency** on real
Nordic hardware, comparing the **open-source Zephyr controller** (`ll_sw_split`) against Nordic's
proprietary **SoftDevice Controller (SDC)**. It exists to answer a concrete robotics-teleoperation
question: **can a BLE link serve as a Wi-Fi-failover safety/control channel** — enough throughput
for telemetry, low enough latency for a stop-signal, on an auditable open stack?

Firmware, configs, prebuilt binaries, and campaign evidence live in this repo. Check each result's
status and retained artifacts in the [evidence index](./docs/EVIDENCE-INDEX.md).

> ### ▶ Reproduce a result: **[REPRODUCE.md](./REPRODUCE.md)**
> Current comparisons require campaign-matched firmware, resolved-config checks, and the physical
> rig. [`run-campaigns.sh`](./run-campaigns.sh) runs every current matched-arm campaign unattended
> (`--dry-run` shows the plan). The prebuilt `run-all.sh` path is an **archival hardware smoke test**,
> not a replay of the current FSU or open-vs-SDC headlines.

## Headline results

The throughput measurements use two Nordic DKs, generally at 2M PHY with Data Length Extension;
on-air FSU acceptance additionally uses an nRF52 observer. **Absolute KB/s and even comparative
deltas depend on the interval, payload, direction, RF arrangement, and run design.** Compare only
matched operating points and consult the linked campaign evidence for confidence and caveats.

| Finding | Result |
|---|---|
| **CoC & GATT throughput** | Close-range, one-way, open stack, receiver-delivered: **CoC 154 → 186 KB/s** FSU off → on at 15 ms / 480 B SDU (n=15, held FSU; 155 → 186 again on 2026-10-06 with batched credit returns, and +20% at 7.5 ms too: see Open vs SDC); **GATT 158 → 189 KB/s** at 7.5 ms (n=8, matched arms), peaking at 193 KB/s at 37.5 ms. The two transports were measured at different intervals and on different runs, so this is not a like-for-like transport comparison. These are one rig's rates, not a universal ceiling or a demonstrated nRF52→nRF54 silicon gain. [CoC](./debug-evidence/coc-fsu-corrected-20260908/) · [GATT](./debug-evidence/gatt-fsu-clean-20260908/METHODOLOGY.md) |
| **Open vs SDC** | **Measured in one session with equal, verified tuning (2026-10-05, n=4 per cell):** open Zephyr is ahead at 7.5 ms on both transports by ~30 KB/s (GATT 157.5 vs 127.0 without FSU, 189.4 vs 158.0 with; CoC 155.8 vs 124.6 without, 186.9 vs 156.2 with). At 15 ms GATT Zephyr leads by ~16 KB/s, while CoC is at parity without FSU (155.4 vs 155.9) and Zephyr +15 with it; from 25 ms the stacks converge (without FSU: GATT at parity, CoC Zephyr +5 to +9 KB/s) and with FSU Zephyr leads by +4 to +18 KB/s (52 µs gap, upstream Zephyr's low-latency default, confirmed on air; SDC reports 65 µs when the FSU request covers 2M only and 70 µs when it also covers 1M, not observable on air). A headroom check showed maximum buffer/event-length tuning changes either stack by ≤ 0.7%, so these gaps are controller behaviour, not configuration. A whole-exchange model fits all 60 one-way cells within 2.7% and suggests why: SDC appears to start an exchange only if a maximum-length reply would still fit (~0.3 ms margin), while Zephyr keeps ~0.27 ms for the exchange as it runs. With short one-way replies SDC leaves ~1 ms of each event unused (4 vs 5 exchanges at 7.5 ms), a cost amortized at long intervals; in duplex the replies are full length and the stacks match (inferred from rates, not observed inside SDC; [model](./debug-evidence/oneway-exchange-model-20261006/README.md)). **CoC FSU gains like GATT on both stacks once the receiver batches its credit returns** (7.5 ms: Zephyr +20.0%, SDC +25.3%). The earlier "CoC FSU ≈ 0% at 7.5 ms on both stacks" is retracted as a CoC property: the recipe sink returned one credit per segment, so every reply carried a 12-byte credit packet and the 6th exchange per event no longer fit (same sink with batched returns: +19.8% / +25.2%, causal test). That sink also depressed SDC's CoC rate at 15 ms (141 vs 156 KB/s), which had made Zephyr look ~16 KB/s ahead on CoC there. CoC rates now come from the cumulative byte counter (the per-second line read ~1% high). The earlier August head-to-head's CoC cells ("parity" at 7.5 ms, "open +19.7%" at 12.5 ms) are quarantined: their sinks' connection-parameter auto-update moved the link to ~50 ms about 5 s in (confirmed by a single-variable test). [Same-session comparison](./debug-evidence/oneway-crossstack-20261005/README.md) (GATT) · [CoC, batched credits](./debug-evidence/oneway-coc-batched-20261006/README.md) (replicated 2026-10-07: [replication](./debug-evidence/replication-20261007/README.md)) · [credit-policy causal test](./debug-evidence/coc-credit-policy-20261006/README.md) · [August confound](./debug-evidence/coc-autoupdate-confound-20261005/README.md) · [Ledger](./docs/EVIDENCE-INDEX.md) |
| **Bluetooth 6 Frame Space Update (FSU)** | The **open controller's** on-air spacing change is accepted by our nRF52-observer protocol at **1M: 150→100 µs** and **2M: 150→52 µs**; neither cell is independently qualified by a professional analyzer, and this does **not** qualify SDC's on-air spacing. At 15 ms / 480 B with FSU held and arms matched, CoC/open gained **+20.4% ±1.0%** (n=15) and CoC/SDC **+11.6% ±0.8%** (n=12) (± = 1.96·SE of paired per-round deltas); the earlier "at 7.5 ms CoC gained ≈0% on both stacks" came from the sink returning one credit per segment (with batched returns: +20% Zephyr, +25% SDC; see Open vs SDC). In the clean GATT sweep, open gained **+16.6–20.2%** and SDC **+11.7–24.6%** across 7.5–50 ms (n=8 per cell, Student-t 95% intervals): **7.5 ms was not a GATT null**. [Open CoC](./debug-evidence/coc-fsu-corrected-20260908/) · [SDC CoC](./debug-evidence/sdc-fsu-corrected-20260908/) · [GATT sweep](./debug-evidence/gatt-fsu-clean-20260908/METHODOLOGY.md) · [1M](./debug-evidence/fsu-q3a-20260812/RESULTS.md)/[2M acceptance](./debug-evidence/q3-2m-accept-20260906/RESULTS.md) |
| **Duplex** | Aggregate rate and directional balance depend on the transport and interval. An almost perfectly even split in the echo rig is an **echo-coupling artifact**; independent CoC traffic splits ~1:1 on both stacks. The "reconnect wedge" (the first CoC connection after the central boots stalling its uplink) was a Zephyr host bug triggered by our central opening the channel with 0 initial credits; with the window sent in the connection request, 14/14 cold and warm reps ran clean. **Duplex FSU (GATT echo, matched held-FSU arms, n=4 per arm): no gain at 7.5 or 15 ms, about +10% at 25 ms on both stacks** (open +9.5% ± 2.1%, SDC +10.8% ± 0.2%; aggregate of both directions). The controller's own counter shows why: FSU helps only when the saved gap time fits one more whole exchange into each connection event (3 → 3 at 7.5 ms, 6 → 6 at 15 ms, 10 → 11 at 25 ms). The nRF52 observer confirms the same counts on air, and the model predicted three further intervals before they were run (12.5 / 22.5 / 35 ms: 0% / +10.6% / +6.9%, counts exact). A second pre-registered set on 2026-10-04 found the predicted large gains between ~0% intervals (13.75 / 16.25 / 18.75 ms: counts exact, +21% / +20% at the first two; 17.5 ms control: no clear gain); the one borderline interval (11.25 ms) missed and narrowed the model's single fitted parameter. **CoC duplex (independent streams, matched arms, credit bugs fixed, 2026-10-06, n=4 per arm; replicated 2026-10-07 with byte-identical firmware, every cell kept its verdict):** Zephyr +0.2% / +2.5% / +9.6% at 7.5 / 15 / 25 ms (184 → 184, 185 → 190, 187 → 205 KB/s), SDC +0.0% / +2.8% / +10.2% (184 → 184, 184 → 189, 186 → 205), split ~1:1 everywhere: with matched controller queue depth the two stacks agree within ~1 KB/s. The Zephyr figures use 20-deep controller TX queues (SDC's controller holds 20); with the earlier 64-deep queues, each side's credit returns waited behind its own queued data and at 25 ms both sides periodically ran dry (FSU off 154.5, gain +23.8%; root-caused on air and with the controller's event counter). The earlier CoC duplex results (open +7.7% / +7.5% / +12.0% with a ~1:2 split, and the one-way events behind them) came from two credit leaks in our test apps and are superseded. **Replicated on 2026-10-04** with byte-identical firmware and the boards further apart: every GATT duplex cell kept its verdict (25 ms: open +10.5%, SDC +9.3%); the CoC cells of that session used the leaky apps. [GATT duplex FSU](./debug-evidence/gatt-duplex-fsu-matched-20261003/README.md) · [SDC, 15 ms + mechanism](./debug-evidence/gatt-duplex-fsu-model-20261003/README.md) · [on-air](./debug-evidence/onair-duplex-20261003/README.md) · [CoC duplex, Zephyr 20-deep](./debug-evidence/coc-duplex-fsu-q20-20261006/README.md) · [CoC duplex, SDC](./debug-evidence/coc-duplex-fsu-fixed-20261006/README.md) · [25 ms diagnosis](./debug-evidence/coc-duplex-25ms-diag-20261006/README.md) · [credit bugs + wedge](./debug-evidence/coc-credit-fixes-20261006/README.md) · [model follow-ups](./debug-evidence/duplex-fsu-followups-20261003/README.md) · [replication](./debug-evidence/replication-20261004/README.md) · [Duplex campaign](./debug-evidence/duplex-campaign-20260906/FINDINGS.md) · [wedge-rate study](./debug-evidence/duplex-stall-rate-20260906/FINDINGS.md) |
| **Latency under load** | Stop-signal RTT tails were measured under concurrent bulk. In the tested GATT configuration (open stack, 7.5 ms, n=2), completion-pacing held p99 at about **28 ms** under saturation, versus ~60 ms unpaced (August, v4.4.1; absolute values differ between sessions); the tested CoC-side pacing did not meet the same tail budget. **FSU on vs off (matched arms, open stack, v4.4.2, n=4 per arm):** under load FSU lowers the stop-signal p99 by ~2–9 ms (~10–25%) at 7.5 ms (unpaced and paced) and 25 ms, with idle latency unchanged and no timeouts; at 150 KB/s, 7.5 ms: unpaced 39.5 → 31.5 ms, paced 22.0 → 19.0 ms. Pacing remains the larger lever. Above the FSU-off ceiling (~150 KB/s at 7.5 ms) FSU carries ~20% more bulk (~180 KB/s) and still keeps the lower tail. The 7.5 ms result replicated on 2026-10-04 (boards further apart, n=2); at 25 ms that placement's tail was pinned at ~71 ms by missed connection events in both arms, masking the FSU p99 gain. **Mechanism (measured, predicted in advance):** at 7.5 ms a full connection event holds 5 bulk exchanges without FSU and 6 with it (controller counter), so queued bulk drains ~20% faster and the stop signal waits less, even below the ceiling, because bulk arrives in bursts. **SDC** shows the same FSU effect (7.5 ms unpaced p99 at 100 KB/s 60 → 47.5 ms; ceiling ~113 → ~135 KB/s), with a higher absolute tail than open in its tuned recipe (extra 10-packet controller queue). This is a rig/configuration finding, not a general safety guarantee. [GATT evidence](./debug-evidence/latency-under-load-20260813/hardening-20260814/README.md) · [FSU latency](./debug-evidence/latency-fsu-20261003/README.md) · [mechanism + SDC](./debug-evidence/latency-fsu-20261004/README.md) · [Overview](./docs/overviews/coc-technical-overview.md) |
| **Reliability (clean-bench soak)** | **25.15 h**, **4,923,931** stop-signal round-trips, and **92** idle→saturation cycles with **0 recorded timeouts, disconnects, or faults** on one paced GATT connection (open stack, 7.5 ms). Availability was **100% at 1 Hz sampling**; this does not establish degraded-RF or rare-event reliability. [Soak evidence](./debug-evidence/latency-under-load-20260813/soak-under-load-20260815/README.md) |

**Terms:** *FSU* = Bluetooth 6 Frame Space Update, which shortens the inter-frame spacing (tIFS) from
150 µs; *held FSU* = the shorter spacing verified to stay in effect for the whole measurement window;
*matched arms* = FSU-on and FSU-off builds whose resolved configs differ only in the requested
spacing; *ABBA* = counterbalanced on/off/off/on run order; *DLE* = Data Length Extension; *SDU* =
L2CAP service data unit; *CoC* = L2CAP Connection-Oriented Channel.

Here, a "professional analyzer" means a commercial Bluetooth protocol analyzer such as the
[Ellisys Bluetooth Explorer 400](https://www.ellisys.com/products/bex400/) or
[Teledyne LeCroy Frontline X500e](https://www.teledynelecroy.com/protocolanalyzer/frontline-x500e-wireless-protocol-analyzer).
The order-of-magnitude purchase cost for this class of instrument is tens of thousands of USD;
exact configurations are quote-priced ([indicative market pricing](https://novelbits.io/bluetooth-low-energy-ble-sniffer-tutorial/)).

Full treatment, statistics, and caveats: **[docs/overviews/](./docs/overviews/)** — start with
[coc-technical-overview.md](./docs/overviews/coc-technical-overview.md).

## Setup under test

- **Hardware:** 2× **nRF54L15-DK** (Bluetooth 6 silicon); earlier runs on 2× nRF52 (nRF52832 /
  nRF52840, Bluetooth 5; selected findings in [`apps/nrf52/nrf52-l2cap-echo/`](./apps/nrf52/nrf52-l2cap-echo/README.md)).
  An **nRF52 DK (nRF52832, PCA10040)** acts as the passive on-air observer.
- **Open stack:** Zephyr **`ll_sw_split`** plus 16 **`fsu-m0`** Frame-Space-Update patches, no
  SoftDevice. August campaigns used **v4.4.1-based** builds whose boot banners read v4.4.1-14/-15:
  they are tree-identical to fork commits `2878980` and `50334aa` (patches 14 and 15 of the
  16-patch series). The September CoC and clean GATT/FSU campaigns used
  **v4.4.2-16**. The fork carries the v4.4.1 line on branch
  [`fsu-m0`](https://github.com/teleop-bench/zephyr/tree/fsu-m0) and the v4.4.2 line on
  [`fsu-m0-v442`](https://github.com/teleop-bench/zephyr/tree/fsu-m0-v442); pin the
  evidence-specific commit rather than assuming a branch identifies every measured tree.
  v4.4.2 is the latest Zephyr release as of 2026-10-07. **Zephyr 4.5 (rc1 out) changes the host's L2CAP receive
  scheduling** (`22896cb8d6f2`: channel RX work moves from the system workqueue to the Bluetooth workqueue, which shifts
  when a receiver's credit returns are processed) and its TX-path locking (`210f14bc4501`). CoC results are sensitive
  to credit-return timing, so re-verify the CoC numbers before citing them for Zephyr 4.5 or later; GATT and the
  controller are not affected by these changes, and upstream has no open-controller FSU (PR #99473 is unmerged).
- **Proprietary stack:** Nordic **SoftDevice Controller** via **nRF Connect SDK v3.4.0**.
- **Transports:** L2CAP Connection-Oriented Channels (credit-based flow control, `seg_recv`) and
  GATT (notify / write-without-response); EATT for a multi-bearer latency arm.

## Reproducing

1. **Current comparisons:** choose a result in the [evidence index](./docs/EVIDENCE-INDEX.md),
   pin its controller/toolchain version, build matched arms and the appropriate responder, check
   the resolved configs, then capture from boot and gate held FSU and operating point. For the clean
   GATT campaign, use branch `fsu-m0-v442` and verify it is at
   `fee9fbc620959b218cda34602d7653d21f7f6a51` (v4.4.2-16).
2. **Archival smoke only:** [`prebuilt-hexes/`](./prebuilt-hexes/) and [`run-all.sh`](./run-all.sh)
   let a two-board rig flash and capture the August matrix without a build toolchain. The runner
   rejects failed checks and **does not print headline deltas**; its old CoC replay was invalidated
   by a silent mid-run FSU revert. Do not use it to confirm current FSU claims.

Full prerequisites (incl. a from-zero Zephyr env bootstrap), recipes, flashing, capture, and the
**silent-failure gotchas** — read those; several failure modes hand you plausible-but-wrong
numbers — are in **[REPRODUCE.md](./REPRODUCE.md)**.

## Repository layout

```
apps/            firmware projects, grouped by target
  nrf54l15/  nrf52/  z44/  coc/  eatt/  esp32/  misc/
docs/            write-ups & plans
  overviews/  plans/  investigations/  references/  legacy/
tools/           host-side tooling (capture-tool.py, analyze.py, …)
prebuilt-hexes/  ready-to-flash firmware + SHA256SUMS
zephyr-patches/  the fsu-m0 controller patch series + provenance
safety/          BLE dead-man's-switch fail-safe reference (heartbeat → external-WDT → STOP)
debug-evidence/  raw logs, configs, per-experiment FINDINGS + manifests
REPRODUCE.md     ← result-specific reproduction recipes and checks
```

## Why this exists (the use case)

The intended customer is a **robotics teleoperation Wi-Fi-failover safety/control link**: a
low-latency heartbeat + stop-signal that stays up when Wi-Fi drops, alongside enough throughput for
concurrent telemetry. Throughput is the table-stakes floor; **latency-under-load and reliability
under degraded RF** are the safety-critical questions. See [docs/overviews/](./docs/overviews/) and
the campaign plans in [docs/plans/](./docs/plans/).

## Honest caveats

- **Compare matched, repeated deltas—not absolutes or unrelated regimes.** KB/s, RTT, and some
  deltas vary with RF environment, interval, payload, and reset state.
- The [evidence index](./docs/EVIDENCE-INDEX.md) gives each result's status and retained proof;
  [REPRODUCE.md](./REPRODUCE.md) gives the applicable recipe and sanity gates. Some older
  prebuilt/campaign results are superseded or retracted.
- **Known limitation: the 1M on-air FSU check is not re-runnable with current tooling.** The accepted
  1M result (150→100 µs) stands on its archived record, but its frozen calibration is bound to analyzer
  versions that predate the 2M port, so a fresh 1M run is rejected at analysis; a re-run would need a
  new calibration campaign. The 2M on-air check is fully re-runnable (`tools/observer-smoke.sh 2m`).
  See REPRODUCE, *On-air FSU observer*.
- A **partial** degraded-RF/range walk exists, including a GATT RTT arm, but clean line-of-sight,
  controlled interference, and full throughput-at-range comparisons remain open.
  [Range evidence](./debug-evidence/rf-range-safety-20260909/PROGRESS.md)

## Origins

This repo began as an ESP32 + zenoh-pico BLE heartbeat proof-of-concept; that original PoC is
preserved at [docs/legacy/zenoh-pico-esp32-poc.md](./docs/legacy/zenoh-pico-esp32-poc.md).

## Related repositories

- **[zephyr @ `fsu-m0` / `fsu-m0-v442`](https://github.com/teleop-bench/zephyr/tree/fsu-m0)** — the
  patched open controller this benchmark runs on (Apache-2.0). The FSU controller code it builds on
  is from upstream Zephyr PRs [#82324](https://github.com/zephyrproject-rtos/zephyr/pull/82324)
  (Andries Kruithof) and [#99473](https://github.com/zephyrproject-rtos/zephyr/pull/99473)
  (Vinayak Kariappa Chettimada); see [NOTICE](./NOTICE).
- The nRF52 raw-radio observer behind the FSU row is in-tree at
  [`apps/nrf52/pca10040-radio-observer/`](./apps/nrf52/pca10040-radio-observer/); its 1M and 2M
  acceptance evidence is in [`debug-evidence/`](./debug-evidence/) (linked in the FSU row above).
  A standalone `ble-fsu-onair-sniffer` repository is not yet public.

The fail-safe STOP (dead-man's switch) is **in-tree** at [`safety/`](./safety/) — it's the
STOP-signal half of the same teleop safety link, a demo use case rather than a novel standalone tool.
Measured on a logic analyzer: last valid heartbeat → physical STOP in 200.091–200.161 ms against a
200 ms deadline (n=4 captures); see [`safety/STOP-MEASUREMENT.md`](./safety/STOP-MEASUREMENT.md).

## License

Apache-2.0 — see [LICENSE](./LICENSE) and [NOTICE](./NOTICE). (Matches Zephyr's license; the
`fsu-m0` controller patches are Apache-2.0 as modifications to Apache-2.0 Zephyr.)

**Firmware images are an exception.** The `*.hex` files contain third-party code, and the 121 images
built with Nordic's SoftDevice Controller (the `*sdc*` hexes) are under Nordic's proprietary
5-Clause license, not Apache-2.0: they may only be used with Nordic chips. See
[THIRD-PARTY-NOTICES.md](./THIRD-PARTY-NOTICES.md).
