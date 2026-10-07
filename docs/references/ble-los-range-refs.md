# BLE line-of-sight range — published reference numbers

Reference material (external), gathered 2026-09-09 to contextualize our own degraded-RF measurement
([`debug-evidence/rf-range-safety-20260909/`](../../debug-evidence/rf-range-safety-20260909/)). We have
**not** run a clean-LOS reach test ourselves (deferred / not planned for now); these are others' numbers.
Cite Argenox/Sheridan for "expected," Nordic's big figures only as an "ideal ceiling."

## LOS range by PHY / TX power
| Config | Range | Character | Source |
|---|---|---|---|
| 1M, 0 dBm, ideal outdoor (chip-to-chip, over-water for Coded leg) | ~682 m (conn) / ~655 m (adv) | **ceiling, not expectation** | [Nordic – Tested by Nordic](https://blog.nordicsemi.com/getconnected/tested-by-nordic-bluetooth-long-range) |
| Coded S=8, 0 dBm, ideal outdoor | ~1,300 m (~2× the 1M number) | ceiling | [Nordic – Tested by Nordic](https://blog.nordicsemi.com/getconnected/tested-by-nordic-bluetooth-long-range) |
| 1M, 0 dBm, favorable LOS (link-budget, real antenna) | ~160 m | defensible "good outdoor" | [Argenox – Maximizing BLE Range](https://argenox.com/library/bluetooth-low-energy/maximizing-bluetooth-low-energy-ble-range) |
| 1M, +10 dBm, favorable LOS | ~295 m | | [Argenox](https://argenox.com/library/bluetooth-low-energy/maximizing-bluetooth-low-energy-ble-range) |
| 1M, 0 dBm, **with a phone in the link** | 0–50 m | commodity peer/antenna dominates | [Argenox](https://argenox.com/library/bluetooth-low-energy/maximizing-bluetooth-low-energy-ble-range) |
| 1M, link-budget estimate | ~100 m LOS / **30–50 m indoors** | typical | [Sheridan Tech – Range of BLE](https://sheridantech.io/2026/07/24/range-of-bluetooth-low-energy/) |
| Coded (long-range), LOS general | 100–1,000 m | | [Nordic DevZone – Coded PHY](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/testing-long-range-coded-phy-with-nordic-solution-it-simply-works-922075585) |
| **2M vs 1M** | **~0.7–0.8×** (≈30% less) | *derived from sensitivity — no clean 2M walk-test found* | [Punch Through – 2M PHY](https://punchthrough.com/crash-course-in-2m-bluetooth-low-energy-phy/) |
| Coded S=8 vs 1M | ~4× ideal (+12 dB) / **~2× real (+8 dB)** | costs 8× airtime | [Nordic](https://blog.nordicsemi.com/getconnected/tested-by-nordic-bluetooth-long-range) · [Hubble/Punch Through](https://hubble.com/community/guides/how-ble-coded-phy-s-8-achieves-4x-range-at-the-cost-of-8x-airtime/) |
| Coded S=2 vs 1M | ~2× (+5 dB) | | [Hubble – Coded PHY](https://hubble.com/community/guides/how-to-use-ble-long-range-coded-phy/) |

## Same-SoC data points (nRF54L15) — the most relevant
- **Nordic declines to publish an nRF54L15 max range** — *"depends on a lot of factors"*; notes HW rev ≥0.9.1 has the optimized matching network. [DevZone Q&A](https://devzone.nordicsemi.com/f/nordic-q-a/117127/nrf54l15-ble-max-range)
- **One DevZone user, nRF54L15 @ +8 dBm, default params, indoor-ish:** connection held to **31 m**, failed **>35 m**, advertising received **>120 m**. Same "solid, then sharp drop; advertising reaches much further" pattern we saw. [DevZone Q&A](https://devzone.nordicsemi.com/f/nordic-q-a/117127/nrf54l15-ble-max-range)
- **Arad MN54L module (nRF54L15), +8 dBm, open-field beach, elevated external antennas:** spec ~2,618 m LOS — vendor ideal, not comparable to a DK on a bench. [Arad](https://www.aradconn.com/news-detail/54LCtoPtest/)
- **nRF52840 proxy:** 1M ~680 m / Coded ~1,300 m @ 0 dBm ideal outdoor. [Nordic](https://blog.nordicsemi.com/getconnected/tested-by-nordic-bluetooth-long-range)

## Independent / academic tests
- **Rutronik whitepaper** (field): ~527 m Coded vs 455 m 1M outdoors; **~60 m vs ~56 m indoors — the Coded advantage nearly vanishes indoors.** [PDF](https://www.rutronik.com/fileadmin/Rutronik/Downloads/printmedia/products/06_wireless/bluetooth5.pdf)
- **Academic:** "Experimental Performance Evaluation of BLE 4 vs BLE 5 Indoors and Outdoors." [ResearchGate](https://www.researchgate.net/publication/320196487_Experimental_Performance_Evaluation_of_BLE_4_vs_BLE_5_in_Indoors_and_Outdoors_Scenarios)
- **Fanstel module, Coded, elevated/high-gain:** 3,200–4,500 m — marketing-grade ideal. [DevZone thread](https://devzone.nordicsemi.com/f/nordic-q-a/63346/nrf52840---long-distance-range-up-to-1-3-km)

## What dominates real range (sourced)
- **Sensitivity sets the budget:** 1M ≈ −95…−97 dBm, **2M ≈ −93 dBm (worse)**, Coded S=8 ≈ −103…−106 dBm. ~6 dB extra budget ≈ 2× range in free space. [Nordic](https://blog.nordicsemi.com/getconnected/tested-by-nordic-bluetooth-long-range)
- **Indoors ≈ 30–50% of LOS** after wall loss + multipath + body absorption. **Drywall ~3–6 dB each** (usually still connects); **brick/concrete/metal much worse — metal is severe** (reflection/blockage, 10+ dB or full shadow). [Sheridan](https://sheridantech.io/2026/07/24/range-of-bluetooth-low-energy/); [Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/99543/indoor-ble-range-improvements)
- **Antenna (PCB vs external) and the peer device matter as much as PHY** — Argenox's 160 m → 0–50 m drop is entirely the phone/antenna, not the radio.
- **2.4 GHz interference + TX power** are the other big levers; **Coded's longer airtime makes it *more* vulnerable to intermittent noise** despite better sensitivity.

## Bottom line for our config (2M PHY, ~0 dBm, PCB antenna, DK↔DK)
Expected **clean-LOS ≈ 25–50 m indoor / ~100 m-class outdoor** (1M favorable LOS ~160 m × 2M's ~0.7–0.8× × indoor derate). Our measured result — **solid to ~40 ft (~12 m), gone by ~80 ft (~24 m) through 3 walls + a large metal object** — is fully consistent: ~20–30+ dB of obstruction loss (3 drywall walls + metal) collapses a ~30–50 m clean-indoor range to ~12 m solid / ~24 m dropout, exactly as predicted. So our through-obstruction number implies a **clean-LOS range on the order of tens of meters indoors / ~100 m-class outdoors for 2M @ 0 dBm** — a defensible statement.

**Range levers if a deployment needs more (all untested by us):** Coded PHY (~2× real, at throughput cost; and *worse* under interference / negligible indoors per Rutronik), higher TX power (+8 dBm), external antenna. 2M is the *shortest*-range PHY — chosen here for throughput/latency, not reach.

## Caveats on the sources
- Vendor km-scale figures are **ideal open-field / elevated external antennas** — not our regime. Use as ceilings only.
- **No clean 2M absolute-range walk-test exists** in what was found; the 0.7–0.8× is derived from sensitivity, state it as a ratio.
- Coded multiplier: theoretical 4× (S=8) vs Nordic's ~2× real vs Rutronik's "≈0 indoors" — present as "~2× real, up to 4× ideal, minimal indoors."
