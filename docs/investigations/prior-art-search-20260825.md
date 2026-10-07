# Prior-art / literature search log — 2026-08-25

Archived so the two novelty claims in `coc-technical-overview.md` (§3 FSU-throughput, §4
CoC-under-degraded-RF) are backed by a documented, repeatable search rather than an unrecorded
one. Two independent searches were run (real WebSearch + WebFetch, US-region, English). Verdicts:
**both claims SUPPORTED** — no directly comparable published measurement found. Full method,
queries, sources, closest prior art, and search limits below. Neither search is exhaustive; the
limits sections list what could not be reached (paywalled PDFs, bot-blocked pages, non-English,
member-only vendor docs).

---

## Claim 1 — "No published *measured throughput* figure attributable to Bluetooth 6.0 Frame Space Update (FSU) / reduced tIFS (150 µs -> ~52 µs)"

**Verdict: SUPPORTED (moderately-high confidence).** After ~20 queries + 9 page fetches across
academic, vendor (Nordic/Silabs/TI/NXP/Ezurio), community blogs (Novel Bits/Punch
Through/Interrupt), Bluetooth SIG, and code hosts (Zephyr/GitHub), no source reports a measured
throughput figure (KB/s or Mbps) *isolated to and attributable to* FSU / reduced tIFS.

### Queries run
**Feature / general:** "Bluetooth 6.0 Frame Space Update throughput measurement"; "frame space
update" Bluetooth throughput KB/s; Bluetooth SIG core 6.0 frame space update throughput benchmark
data rate improvement.
**Protocol / IFS-specific:** BLE reduced T_IFS interframe space throughput Mbps measured; "tIFS"
OR "T_IFS" 52us Bluetooth throughput measured connection event packets per event; reduced
interframe space BLE connection throughput packets per connection event measured hardware nRF;
"frame space update" FSU BLE throughput "150" "52" measured demo webinar.
**Academic:** Bluetooth 6.0 reduced interframe spacing throughput experiment IEEE arXiv;
Bluetooth low energy variable inter frame space throughput evaluation paper 2025.
**Vendor:** Nordic SoftDevice Controller frame space update FSU throughput;
"sdc_support_lowest_frame_space" OR "frame space" Nordic devzone throughput test result; Silicon
Labs Bluetooth 6.0 shorter interframe space throughput increase percent; TI NXP Bluetooth 6.0
reduced tIFS interframe space throughput application note; nRF54L15 Bluetooth 6 frame space update
throughput increase measured Mbps.
**Community blogs:** Novel Bits Bluetooth 6.0 frame space update throughput; Punch Through OR
Interrupt Memfault Bluetooth 6 frame space tIFS throughput measurement.
**Code hosts:** Zephyr LL_FRAME_SPACE_REQ frame space update throughput benchmark; Zephyr
bluetooth controller frame space update pull request; "CONFIG_BT_FRAME_SPACE_UPDATE" Zephyr
throughput; "frame space update" OR "reduced IFS" BLE throughput result site:github.com.

### What exists in the public record (classified)
- **Feature-description only, no number:** Silicon Labs blog "The New and Improved Bluetooth 6.0";
  Ezurio "Bluetooth 6: What's New" (only "shorter IFS *can* improve throughput"); CNX Software
  2024-09-04 (T_IFS negotiable 50-1000 µs); Bluetooth.com Core 6.0 overview; Nordic DevZone
  "What's new in Bluetooth Core 6.2"; Nordic nRFxlib SDC CHANGELOG / nRF Connect SDK v3.2.0
  (announces LE Frame Space Update HCI + `sdc_support_lowest_frame_space`); Zephyr
  `CONFIG_BT_FRAME_SPACE_UPDATE` / `frame_space_updated` API. All CONFIRMED via fetch where noted.
- **Theoretical math, still fixed 150 µs IFS (does not even model FSU):** Silicon Labs throughput
  docs v6.0.0/v6.2.0 (e.g. (251-4-3)/2500 µs = 97,600 B/s); Interrupt/Memfault "A Practical Guide
  to BLE Throughput"; Punch Through throughput series.
- **Simulated (not hardware) throughput vs IFS:** MathWorks "Evaluate Performance of ACL
  Interframe Space in a Bluetooth LE Piconet" — MATLAB Bluetooth Toolbox simulation sweeping IFS
  = 10/50/100/150/200/250 µs, concludes smaller IFS -> higher throughput, but shows a plot with
  **no numeric values** and is **simulated, not measured**.
- **Names FSU next to a number, but number not attributed to FSU:** Novel Bits "BLE Shorter
  Connection Intervals: Hands-On with nRF54L15" (~Apr 2026) — nRF54L15 FSU frame space ~63 µs,
  peak throughput ~1.5 Mbps, "FSU highly recommended." The ~1.5 Mbps is tied to
  connection-interval/SCI behaviour, **not** isolated as an FSU-on vs FSU-off delta.

### Closest prior art
(a) **MathWorks MATLAB ACL-IFS example** — only source plotting throughput vs sub-150 µs IFS in a
BLE-6 variable-IFS context, but *simulated* and *no numbers*. (b) **Novel Bits nRF54L15 SCI
hands-on** — closest hardware writeup naming FSU + a throughput number, but the number is
attributed to shorter connection intervals, not FSU; no controlled FSU-on/off delta.

### Search limits
Argenox "Bluetooth 6 Speed: Maximizing BLE Data Throughput" (the single most likely blog to hold
a real number) returned **HTTP 403 on every fetch** — body not inspected; snippets showed only
generic IFS language. element14 nRF54L blog timed out. WebSearch does not deeply index IEEE
Xplore / ACM full text, SIG member-only docs, gated DevZone threads, webinar/conference slide
decks, or talk video. Non-English not covered. Net: no measured FSU-attributable figure is
publicly *indexed and readable*; cannot fully exclude one inside the Argenox page, a paywalled
paper, or a webinar deck.

---

## Claim 2 — "No published measurement of L2CAP CoC throughput under degraded RF — let alone one comparing CoC vs GATT as a control arm"

**Verdict: SUPPORTED (high confidence).** 21 queries across 3 buckets + targeted fetches. Every
RF-throughput study is transport-blind (generic connection / notifications / PHY-layer PER);
every CoC-specific throughput result is either ideal-condition or a clean-link credit/scheduling
bug. No source sits in the intersection (CoC x degraded-RF x GATT-control).

### Queries run
**Bucket A — CoC-vs-GATT throughput comparisons:** L2CAP CoC vs GATT throughput comparison BLE;
L2CAP CoC throughput measurement credit based flow control BLE; BLE L2CAP credit based flow
control throughput measurement Zephyr; L2CAP CoC throughput degradation under interference
measurement compared GATT notifications; GATT notification vs L2CAP throughput reliability packet
loss experiment robot teleoperation; Silicon Labs BLE L2CAP CoC throughput RSSI range test kB/s
GATT comparison.
**Bucket B — CoC credit-flow stalls:** L2CAP CoC credit stall throughput drop; Zephyr L2CAP CoC
throughput issue credit github; NimBLE L2CAP CoC throughput credit based flow control issue; L2CAP
connection oriented channel throughput packet loss retransmission BLE performance; Nordic DevZone
L2CAP CoC throughput degraded range packet loss; L2CAP CoC credit return round trip stall packet
loss worse than GATT notification throughput collapse.
**Bucket C — BLE throughput vs RF (transport-blind check):** BLE throughput Wi-Fi interference
measurement goodput packet error rate; Spork BLE reliability interference 2.4GHz measurement; BLE
throughput vs distance RSSI interference degradation experiment; Michael Spörk BLE reliability
connection throughput interference thesis; credit based flow control BLE performance interference
packet loss stall academic; "connection oriented channel" BLE reliability interference measurement
wireless credit; "L2CAP" OR "CoC" BLE throughput interference channel error credit round trip
stall measurement IEEE; BLE DFU OTA L2CAP throughput interference range reliability measurement
Punch Through Novel Bits; "Bluetooth Low Energy Reliability and Throughput under Wi-Fi
Interference" authors GATT L2CAP transport method.

### What exists in the public record (classified)
- **Bucket A — CoC-vs-GATT, ideal/close-range only, never RF:** "Performance Boost: Using L2CAP
  Socket Over GATT" (Medium / Bluetooth Demystified, N. Savant) — CoC saves only the ~3 B ATT
  header ~= ~1% (closest CoC-vs-GATT, zero RF axis); Nordic DevZone #61028 "L2CAP vs GATT for max
  throughput with iOS" (qualitative, ideal); Punch Through "BLE Throughput Fundamentals";
  Interrupt/Memfault "A Practical Guide to BLE Throughput"; Silicon Labs "Throughput" docs
  (~97.6 KB/s theoretical); TI BLE5-Stack L2CAP guide (API/spec).
- **Bucket B — CoC stalls, ALL clean-link config/scheduling/buffer bugs, NOT RF:** Zephyr #69975
  "Low throughput over RX L2CAP CoC" (250->30 kbps after 3.4.0 hardcoded RX credits to 1); Zephyr
  #19922 (linear-time credit give); Zephyr #28384/#2627/#20640 (SDU seg / buffer starvation);
  Apache NimBLE #818 "throughput inconsistent and fluctuating" (~8 -> ~65 kB/s on an idle link);
  STM32 community #652299 (one packet per interval); Nordic DevZone #87529 (Android no-DLE for
  CoC), #120931 (optimizing L2CAP throughput); MicroPython flow-control fix. **Every in-the-wild
  CoC collapse is a credit/buffer/scheduling defect on a good link — none inject or measure RF.**
- **Bucket C — BLE throughput vs RF, real but transport-blind (never split CoC vs GATT):** Pang et
  al. "BLE Reliability and Throughput under Wi-Fi Interference," IEEE 2022 (KU Leuven) — generic
  connection, closest RF-throughput prior art but transport-blind; arXiv 2405.01231 "Modeling the
  Trade-off between Throughput and Reliability in a BLE Connection" (same group, connection-level
  model + RF validation); Spörk et al. "Improving the Reliability of BLE Connections," EWSN '20
  (Wi-Fi interference, blacklisting/PHY, PDR ~98.6%); arXiv 2105.00141 "PER Measurement of BLE in
  RF Interference and Harsh EM Environment" (PHY-layer PER, SDR, reverberation chamber); RG
  339647017 "Performance Evaluation of BLE Under Interference" (~28% goodput loss from Wi-Fi);
  Pang et al. Sensors 2021 s21072257; INL/RPT-23-74719; arXiv 2301.08109; arXiv 2408.11499.

### Closest prior art per bucket
- CoC-vs-GATT: Bluetooth-Demystified Medium article — real numbers, strictly ideal, no RF axis.
- CoC stall under loss: NimBLE #818 / Zephyr #69975 — genuine CoC collapse, but clean-link
  credit/scheduling bugs, not RF.
- RF-driven throughput: Pang et al. IEEE 2022 + arXiv 2405.01231 — real Wi-Fi-interference
  degradation, but generic BLE connection, never isolating CoC, never a GATT control arm.

### Search limits
WebSearch US-region/English. Three primary PDFs inaccessible to the fetch model — ResearchGate
365122470 (403), IEEE 9920206 (403), and PDFs returned as un-parsed binary (Spörk, arXiv
2105/2405); their transport classification rests on abstracts/snippets, not full-text — a buried
CoC experiment in the Pang full text cannot be 100% excluded. Some Silicon Labs/TI throughput-
tester appnotes behind logins not fully retrieved (ideal-condition tools regardless). Zephyr/
NimBLE/BlueZ issue trackers not exhaustively crawled, but the credit-stall pattern was uniformly
clean-link across those surfaced.

---

## Bottom line
Both novelty framings are supported by an absence of directly comparable published work: (1) no
measured FSU-attributable throughput figure; (2) no CoC-throughput-under-degraded-RF measurement,
and none with a GATT control arm. Stated as *novel to our knowledge from a documented,
non-exhaustive search* — not a priority claim. Re-run and append here if revisited.
