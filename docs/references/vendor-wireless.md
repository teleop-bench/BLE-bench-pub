# Wireless tech-stack landscape for shipyard teleop robotics

**Purpose.** Situate our BLE 5/6 benchmark work against the *primary* wireless radios that field
teleoperation and unmanned-ground/air systems, so we can position the BLE link honestly for a
shipyard-teleoperation robotics customer (steel hulls → Faraday-cage reflections + dense
multipath; welding/motor EMI; congested 2.4/5 GHz Wi-Fi). Requirements the customer must satisfy:
low-latency control loop + video downlink + an independent safety/failover link.

**How to read this.** Every spec below is from a primary vendor datasheet or first-party page;
URLs inline. Vendors frequently withhold hard **range** and **latency** numbers ("antenna/terrain
dependent") — those gaps are flagged. Treat all vendor range/latency marketing as **LOS best-case,
not steel-hull reality.** Source-access caveats are collected at the end.

Notation: MANET = self-forming/self-healing mesh; PtP/PtMP = fixed point-to-(multi)point;
SWaP = size/weight/power; "eff." = effective TX power after beamforming.

---

## 1. Vendor spec tables

### Doodle Labs — Nano² (Mesh Rider family) — the reference OEM module
| Spec | Value |
|---|---|
| Bands (Commercial M1–M6) | 1625–2500 MHz |
| Bands (Defense, coming) | 1350–1390 + 2200–2500 MHz (L+S); dual-band 900 MHz + 2.4 GHz |
| Channel BW | 20 / 15 / 10 / 5 / 3 MHz |
| Max TX power | 1.6 W (32 dBm) |
| MIMO | 2×2 |
| Throughput | 80 Mbps |
| C2 latency | **1.5–10 ms** (ultra-reliable low-latency channel) |
| Range | 330 km field-proven (LOS, marketing) |
| SWaP | 46.9 × 35 × 20.7 mm, 48.3 g, 5 W avg / 8 W peak |
| Encryption | FIPS 140-3, AES-256/128 |
| Interfaces | 100Base-T Eth, USB dev+host, UART×1, GPIO×3, MMCX |
| Modes | Mesh, Relay, P2MP, WDS AP/Client, Gateway |

Source: doodlelabs.com/product/nano2. Family context: Mesh Rider is a DoD-sponsored self-forming
MANET waveform (COFDM, adaptive BPSK→64QAM); Nano² is the smallest module (48.3 g) up to Boost
(147 g). Newer **Helix** does dedicated C2 + 4K-video channels, 25 g two-board, 80 Mbps, 50+ km
(doodlelabs.com/news/introducing-the-smart-radio-helix).

### Silvus StreamCaster — MN-MIMO MANET
| Spec | SC4400E (4×4) | SC4200E (2×2) |
|---|---|---|
| Bands | 300 MHz–6 GHz (UHF/900/L/S/C/5 GHz options) | 400 MHz–6 GHz |
| Channel BW | 20 / 10 / 5 (2.5 / 1.25 opt) | — |
| Throughput | up to 100 Mbps (adaptive) | up to 100 Mbps (adaptive) |
| **Latency** | **7 ms avg @ 20 MHz** | **7 ms avg @ 20 MHz** |
| TX power | 1–20 W native / up to 80 W eff. | 4 W / 8 W eff. |
| Nodes | 550+ | — |
| Robustness | MAN-IA (interference avoidance), MAN-IC (cancellation), spectrum analyzer | MAN-IA opt. |
| Encryption | AES-256 opt, FIPS 140-3 L2 | AES-256 opt, FIPS 140-2 L2 Suite B |
| SWaP | 134×115×46 mm, 861 g, 8–100 W; OEM 258 g | 101×67×38 mm, 425 g; OEM 116 g |
| Env | IP68 (20 m), −40/+85 °C | IP68, −40/+65 °C |

Sources: silvustechnologies.com StreamCaster-4400 (SC4400E) and StreamCaster-4200 enhanced
datasheet PDFs. **Range in km is not published** (antenna/terrain-dependent).

### Rajant Kinetic Mesh (InstaMesh) — multi-transceiver mesh
| Model | Transceivers / Bands | PHY rate | TX power | SWaP |
|---|---|---|---|---|
| ES1 | 2 tx; 2.4 + 4.9/5 GHz | 300 Mbps/tx | 29 dBm | 455 g, 2.8 W idle/15 W pk, IP67 |
| LX5 | up to 4 tx; **900 MHz** + 2.4 + 5 GHz | 900:54–65, 2.4/5:300 | 30/29/28 dBm | 1850 g, 8 W idle/33 W pk |
| ME4 | 2 tx; 900 MHz + 2.4/4.9/5 GHz | 900:65, others 300 | 30/29/23/28 dBm | 1162 g, 5.5 W idle/19 W pk |
| Peregrine | 4 tx (2×2.4 + 2×5 GHz) | up to 2.3 Gbps aggregate | 30 dBm | 2946 g, 10 W idle/34 W pk, M12 Eth |

Sources: secure.rajant.com public datasheet PDFs (ES1 #1479, LX5 #426, ME4 #256), Peregrine
sheet. Architecture: InstaMesh Layer-2, no root node, make-make-break/never-break multi-link.
Encryption AES-256 GCM/CTR per-hop + per-packet auth (Suite B is a separate, non-default SKU).
Markets explicitly include **seaports** (ES1, ME4). **No numeric latency or range published** on
any sheet — qualitative "low latency" only. Sub-GHz (900 MHz) is SISO, only on LX5/ME4.

### Persistent Systems — Wave Relay MPU5 / Embedded Module (3×3 MANET)
| Spec | MPU5 | Embedded Module |
|---|---|---|
| Bands (swappable RF modules) | L 1350–1390, BAS 2025–2150, S 2200–2507, C 4400–5000 & 5100–6000 MHz | same line |
| Channel BW | 5 / 10 / 20 MHz | — |
| MIMO | 3×3 (SISO→3×3), MRC + spatial mux | 3×3 |
| Throughput | 100+ Mbps (up to ~150 peak) | ~same class |
| TX power | 6–10 W | 6–10 W |
| Node entry / hops | <1 s / no limit; 130 mi max node dist | same |
| SWaP | 391 g chassis, IP68, MIL-STD-810G/461F | **90.7 g**, ~3.6 W |
| Encryption | CTR-AES-256, HMAC-SHA-256, FIPS 140-2, CNSA/Suite B | same |
| Interfaces | Eth, USB OTG/host, RS-232, SDI/HDMI video, onboard Android | Eth, USB OTG, RS-232, micro-HDMI |

Sources: persistentsystems.com MPU5 spec (03EN070) + Embedded Module (03EN190). **No published ms
latency** — closest metric is "<1 s node entry." Peer-to-peer (not TDMA).

### Microhard — pDDL / pMDDL / Nano (OEM embedded)
| Model | Bands | MIMO | Throughput | RF pwr | SWaP |
|---|---|---|---|---|---|
| pDDL2450 | 2.4 GHz | SISO | ~25–28 Mbps @8 MHz | up to 1 W | 5 g |
| pMDDL2450 | 2.4 GHz | 2×2 (MRC/LDPC) | ~25–28 Mbps | up to 1 W | 7 g |
| pMDDL1624 | **1.6–2.4 GHz** (6 sub-bands) | 2×2 | ~21 Mbps | up to 2 W | 15 g; FHSS+DFS |
| pMDDL5824 | 2.4 & 5 GHz | 2×2 | ~25–28 Mbps | up to 1 W | 20 g |
| fDDL9324 | **900 MHz**–2.5 GHz (8 bands) | 2×2 | >21 Mbps | up to 1 W | 5.5 g; FHSS+DFS, AES-256 |
| Nano n920X2 | 900 MHz FHSS | SISO | 19.2–345 kbps | 0.1–1 W | 15 g; **60+ mi**, TDMA |

Source: microhardcorp.com brochures. Adaptive BPSK→64QAM, 32-bit CRC + ARQ, AES-128/256
(export-gated). **No ms latency published**; only the 900 MHz Nano telemetry modem publishes range.

### Ubiquiti airMAX (fixed-infrastructure baseline — NOT MANET, for contrast)
| Spec | Rocket M5 / M-Titanium |
|---|---|
| Freq | 5470–5825 MHz (also 900 MHz / 2.4 GHz variants) |
| Architecture | airMAX **TDMA** PtP/PtMP (AP-controlled star) |
| MIMO | 2×2 |
| Throughput | 150+ Mbps real TCP (Titanium) |
| Range | 50+ km LOS |
| SWaP | 16×8×3 cm, 0.5 kg, 8 W, 24 V PoE |

Source: dl.ubnt.com datasheets. Latency stated only qualitatively ("TDMA lowers latency as the
network scales"). Not self-healing mesh — included only as the fixed-link contrast point.

---

## 2. Insights for the shipyard teleop customer

**(1) Band + architecture choices for harsh-RF / NLOS — and what they imply for a steel hull.**
Three consistent moves across the serious MANET vendors, all directly relevant to a steel yard:
- **Sub-GHz / L-band for penetration + multi-band diversity.** Rajant LX5/ME4, Silvus,
  Persistent, and Microhard all offer 900 MHz–L-band options because lower frequencies diffract
  around and penetrate structure far better than 2.4/5 GHz. Inside a steel hull, 5 GHz is
  largely LOS-only between compartments; **L/S-band (1.3–2.5 GHz — the Nano² and Persistent home
  turf) is the practical penetration/bandwidth compromise.** Pure 2.4/5 GHz gear (Ubiquiti,
  Microhard 2450) is the weakest fit below deck.
- **MIMO to *exploit* multipath, not fight it.** Silvus MN-MIMO (up to 4×4), Persistent 3×3, and
  the Nano²/Rajant 2×2 use spatial multiplexing + MRC. A steel hull's dense reflections are
  pathological for a SISO link but are *signal* for MIMO — decorrelated multipath is exactly what
  spatial multiplexing needs. **In a reverberant metal environment you want more spatial streams,
  not just more power.**
- **MANET self-healing for a robot moving through a maze of bulkheads.** Silvus (550+ nodes),
  Persistent (no hop limit, <1 s node entry), Rajant, and Doodle Mesh Rider self-form/self-heal,
  so a robot rounding a bulkhead re-routes through relay nodes instead of dropping. This argues
  for **dropping cheap relay nodes at hatches/decks** and letting the mesh stitch NLOS coverage —
  a fixed PtP link (airMAX) cannot.

*Defensible primary-link architecture for a steel hull:* **L/S-band + 2×2-or-better MIMO + MANET
mesh with staged relay nodes.** The Nano² sits squarely in that class, as do Silvus SC4200E and
the Persistent Embedded Module.

**(2) Where a BLE 5/6 link fits vs these primary radios — the honest gap.** BLE is **not** a
realistic primary teleop link:
- **Throughput:** our benchmark ~150–170 KB/s ≈ 1.2–1.4 Mbps (matching Nordic's BLE-5 2M-PHY
  app-layer ceiling). Primaries deliver 80–150+ Mbps — **~60–100× more.** Video downlink is
  categorically out of BLE's reach.
- **Range:** BLE ~655 m (1M) to ~1.3 km (Coded-PHY) LOS outdoors; through steel that collapses
  to same-compartment. Primaries claim tens of km LOS. BLE is a **last-meters** technology.
- **Latency:** BLE's one genuinely competitive dimension — our low-latency heartbeat and
  1.5–10 ms control loop are in the *same class* as Doodle's 1.5–10 ms and better than Silvus's
  7 ms avg — but only for a *tiny* control/heartbeat payload.

*Verdict:* BLE 5/6 is a **short-range failover / proximity / commissioning / last-meters safety
link**, defined by latency and independence — never throughput or range.

**(3) Published teleop latency budgets — how they frame our safety-link thesis.** Human-factors
budgets: glass-to-glass video ~100 ms desirable, ~150–170 ms usable ceiling, unusable past
~400–500 ms; dexterous manipulation stricter (~150 ms trust erosion). Vendor link latencies
(Silvus 7 ms, Doodle 1.5–10 ms) are a small slice of that — the primary radio is rarely the
latency bottleneck (the codec + network path are). **The safety/stop-signal loop has a different
latency contract than the video loop**, and this is exactly where our work reframes the
conversation: our latency-under-load finding — a concurrent bulk stream pushes stop-signal RTT
tail from ~12 ms idle to ~35 ms / 99.7% >30 ms when saturated, and completion-pacing to
1-outstanding cuts >30 ms from 100% to 2.5% at the same throughput — is the failure mode the
primary vendors **do not characterize.** They publish an *average* link latency and are silent on
**tail latency of a small safety message competing with a saturated video stream.** Industrial
e-stop practice wants <10–30 ms watchdogs and <500 ms stop; our data shows a naive shared-queue
design blows the watchdog budget under load even when the mean looks fine.

**(4) Redundancy / failover — how a BLE backup complements (not competes with) the primary.** The
vendors' redundancy story is **homogeneous**: band diversity *within one PHY family* (Rajant
multi-transceiver, Silvus dual-frequency, Persistent swappable modules) + mesh path redundancy.
All of it fails **correlated** to a common-mode event — a firmware fault, a jammer over their band
block, or a power-domain fault on the shared radio takes down every diversity path at once,
because it's one vendor's stack on one SoC. A BLE 5/6 backup is **heterogeneous diversity** —
different PHY, different silicon (Nordic vs the primary's ASIC), different stack, different power
domain, different band. It can't carry video, but it can carry the two things that must *never*
drop: **heartbeat + stop-signal.** Complementary architecture:
- **Primary:** Silvus/Doodle-class L/S-band MIMO MANET carries control + video.
- **Backup:** an independent BLE link (own battery/MCU) maintaining a heartbeat, able to assert
  stop the instant the primary's heartbeat lapses — a dead-man's switch on separate silicon.

This occupies the one niche the primaries structurally can't self-provide: **independence from
their own common-mode failures.** It's the wireless analog of a hardwired e-stop deliberately not
on the main bus.

**(5) Positioning angles for our benchmark work** — what the vendors do *not* publish and we do:
- **Tail latency under contention.** No vendor publishes stop-signal tail latency while a video
  stream saturates the link (Silvus gives a 7 ms *mean*; Rajant/Persistent/Microhard give
  nothing). Our latency-under-load + completion-pacing result is novel and safety-relevant.
- **CoC vs GATT under RF / under load.** Nobody in this field asks the question — every vendor
  assumes their own proprietary PHY. Our open-stack comparison (and the finding that
  **segmentation, not transport, drives loss-sensitivity** — see `coc-technical-overview.md` §4)
  is unique framing.
- **Reproducible open methodology.** Where these vendors publish anything, it's marketing
  datasheet figures with range/latency frequently withheld. Our benchmark is reproducible and
  open — a credibility asset when a customer wants to *verify* rather than trust a datasheet.
- **The heterogeneous-backup thesis itself.** Vendors sell homogeneous diversity; the independent
  different-silicon safety link is whitespace none of them occupy.

---

## 3. Where our BLE benchmark adds value — and where it honestly does not

**Adds value:**
- As the **failover safety link** (heartbeat + stop-signal on independent silicon) — a role the
  primary vendors can't fill for themselves. Latency competitive; independence is the point.
- **Tail-latency-under-load characterization** — a real gap in every vendor's published data.
- **Commissioning / proximity / last-meters** control where a full MANET radio (48 g, 5 W,
  100+ Mbps) is overkill for a hatch-side pairing task.
- **Reproducible open benchmarking** as a verification counterweight to withheld datasheet numbers.

**Does NOT add value (be upfront):**
- **Primary link.** ~1.4 Mbps and 100s-of-meters (collapsing through steel) cannot carry teleop
  video or long-haul NLOS control. Do not position BLE against 80–150 Mbps MIMO MANET on
  throughput or range — it loses by 60–100× and tens of km.
- **Multipath / steel-hull penetration as a strength.** BLE's 2.4 GHz PHY is *more* vulnerable to
  steel-hull NLOS than the sub-GHz/L-band + MIMO the primaries use; Coded-PHY buys range at
  throughput cost but doesn't make BLE a hull-penetrating workhorse.
- **Band diversity / anti-jam.** BLE has no MAN-IA-class interference avoidance, no 4×4 spatial
  multiplexing, no mainstream sub-GHz option. Its value is *independence and simplicity*, not RF
  robustness.

---

## 4. Source / evidence caveats
- **Range is withheld** by Silvus, Rajant, Persistent, Microhard (DDL video), and effectively by
  Ubiquiti indoors — all published range is LOS marketing best-case.
- **Latency (ms) is published only by Silvus (7 ms avg) and Doodle (1.5–10 ms).** Rajant,
  Persistent, Microhard, and Ubiquiti publish **no ms latency** — qualitative "low latency" only.
- Doodle per-model datasheet PDFs are anti-bot / 301-gated; the **Nano² product-page numbers are
  solid**, but sibling-model latency/FIPS figures come from datasheet search excerpts.
- Rajant Suite-B certification, MPU5 exact wattage, "28 ms MANET video," and the
  dexterous-manipulation thresholds are secondary/vendor-blog sources, not lab-verified.
- Teleop human-factors budgets (~100/150/400 ms) are from teleop-latency literature/industry
  sources, directional not standards-binding.
- Treat every number here as a starting point for the customer's own on-site RF survey — none of
  it substitutes for measuring the actual hull.

*Compiled 2026-08-25 from a documented (non-exhaustive) web/datasheet search. Companion to
`coc-technical-overview.md` (transport findings) and the latency-under-load / safety-link work.*
