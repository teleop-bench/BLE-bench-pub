# PCA10040/nRF52832 Raw-Radio Observer Plan

**Status:** Revision 2 plan only; every bench campaign requires a frozen preregistration before data collection.  
**Recorded:** 2026-08-08  
**Hardware:** Existing PCA10040 nRF52 DK with nRF52832  
**Purpose:** Independent on-air packet-spacing observation for the two-nRF54L15 FSU bench.

## 1. Objective and claim boundary

Turn the existing PCA10040/nRF52832 DK into an independent, single-channel RF timing observer for the two nRF54L15 devices under test. The observer measures packet-to-packet timing; it does not decode or validate the complete Bluetooth 6 Frame Space Update procedure.

If qualified, the observer can support this claim:

> An independent receiver measured the steady-state interval between CRC-valid BLE packets on one connection data channel. The measured interval changed by the amount predicted from the HCI-selected frame spacing.

It does not establish:

- FSU procedure or Bluetooth Core conformance.
- Complete connection coverage.
- Direction attribution unless that is separately implemented and validated.
- Equivalence to a commercial protocol analyzer.

The first gate is deliberately independent of FSU: the observer must resolve hardware-scheduled synthetic packet pairs at 150, 100, 70, and 52 us before it attempts connection discovery or observes an FSU procedure. SDC is HCI-verified/on-air-pending and therefore cannot serve as the observer's first physical positive control.

## 2. Architecture

```text
nRF54 central  <->  nRF54 peripheral
                         |
                         | RF, receive only
                         v
               PCA10040 / nRF52832
             raw RADIO + PPI + TIMER0
                         |
                RAM records/histograms
                         |
                   UART after freeze
                         v
                  Host-side analyzer
```

The observer remains passive and continuously in RX. It never performs the RX-to-TX turnaround that could introduce a 150 us timing assumption.

Before the connected bench is used, one nRF54 DK temporarily acts as a fixed-channel raw packet-pair generator:

```text
nRF54 raw burst generator
  hardware-scheduled 150/100/70/52 us pairs
                         |
                         | RF
                         v
               PCA10040 / nRF52832
                 calibration gate
```

## 3. Firmware foundation

Use a dedicated Zephyr application targeting:

```text
nrf52dk/nrf52832
```

Configuration principles:

- `CONFIG_BT=n`: the Zephyr Bluetooth controller must not own RADIO, PPI, or TIMER0.
- Use direct Nordic HAL/nrfx register access.
- Keep assertions enabled.
- Do not log while capture is active.
- Emit UART output only after the capture is frozen.
- Use fixed-size RAM records with explicit overflow and drop counters.

At startup, print:

- Firmware Git hash.
- Board target.
- `FICR.INFO.PART`, `FICR.INFO.VARIANT`, and relevant silicon revision.
- PHY, channel, access address, and CRC initialization.
- Timer frequency.
- Ring capacity and final drop count.
- Applicable nRF52832 errata and any applied workaround.

Check the silicon variant against the nRF52832 errata before accepting results. The PCA10040 board revision does not by itself establish the SoC revision.

## 4. Synthetic timing calibration

Before parsing `CONNECT_IND`, temporarily turn one nRF54 DK into a raw fixed-channel packet generator. This is an instrument calibration, not an FSU test.

**This generator is CRITICAL-PATH day-one work**: Q1 (and therefore the
whole go/no-go) cannot run without it, and its schedule must be
hardware-driven and self-validated. Software-delay-scheduled pairs do NOT
count as Q1 evidence — the pair spacing must come from TIMER/DPPI and be
confirmed by the generator's own PHYEND/start timestamps.

The generator must:

- Use TIMER/DPPI scheduling rather than software delays.
- Record its own PHYEND/start timestamps.
- Send pairs of valid BLE-formatted packets at 150, 100, 70, and 52 us spacing.
- Put a distinctive sequence number in every pair.
- Transmit at a low repetition rate, initially one pair every 10 ms.
- Exercise 1M and 2M separately.
- Use packet sizes representative of the later FSU tests.
- Run with each nRF54 DK in turn as the transmitter.

The observer calibration passes only when:

- Both packets are present for every scheduled pair, or loss is explicitly bounded by a frozen acceptance rule.
- Received packets are CRC-valid and sequence pairing is unambiguous.
- Four clearly separated timing populations are present.
- Measured changes match -50, -80, and -98 us within an uncertainty bound derived from the calibration data.
- Results do not materially depend on which nRF54 board transmits.
- Observer timestamp, overwrite, and ring-drop counters remain clean.

If 70 or 52 us synthetic pairs cannot be retained reliably, stop. Do not invest in connection discovery, channel handling, or FSU analysis.

## 5. Connection discovery

The first implementation deliberately simplifies the bench:

1. Configure the nRF54 peripheral to advertise on channel 37 only.
2. Start the PCA10040 on advertising channel 37 at BLE 1M.
3. Filter for the known peripheral address.
4. Parse its `CONNECT_IND`.
5. Extract:
   - Access address.
   - CRCInit.
   - Data-channel map.
   - Connection interval.
   - Channel-selection flag.
6. Select one enabled data channel, preferably the lowest enabled channel.
7. Reconfigure the observer for that channel and the planned PHY.

The observer does not need to follow hopping. It remains parked on one enabled data channel and captures connection events whenever the DUT connection visits that channel. Consecutive packets within an event use the same channel.

For 2M runs, the observer may switch directly to 2M after parsing the 1M `CONNECT_IND`. It will begin receiving once the DUT connection completes its PHY transition.

Start with the NORMAL, stable channel map (do not reduce it initially — a
reduced map adds controller work and another experimental variable). With
37 channels at a 7.5 ms interval, a parked channel is visited on average
roughly every 277 ms, so ~100 observations take ~28 s; use 30–60 s
captures. Only if acquisition proves inadequate, THEN consider freezing a
reduced (e.g. two-channel) map and parking on one of its channels. Either
way, reject a run if the map changes after connection establishment.

## 6. Raw RADIO configuration

Configure:

- `MODE = Ble_1Mbit` or `Ble_2Mbit`.
- Correct BLE preamble length.
- Four-byte access address.
- BLE packet header and length layout.
- BLE CRC polynomial and captured CRCInit.
- Data whitening enabled with the parked channel as IV.
- `READY_START` shortcut.
- Repeated receive using `END_START`.
- CRC events enabled.
- High-priority `END` interrupt.
- No disable/re-enable between packets.

Use the same RX buffer repeatedly. Do not copy full payloads. But the record MUST carry enough to satisfy the sequence-pairing, transmitter-distinction, PDU-length, and RSSI requirements stated elsewhere in this plan — the earlier minimal struct (timestamps + CRC + flags) was insufficient. Copying a tiny header/tag (a few bytes: the PDU length, and a marker byte) is compatible with the "no full payload" constraint. In the END ISR, copy only this fixed-size record before the next packet begins:

```c
struct capture_record {
    uint32_t address_ticks;   /* TIMER0 CC[1] at RADIO EVENTS_ADDRESS  */
    uint32_t end_ticks;       /* TIMER0 CC[2] at RADIO EVENTS_END       */
    uint16_t observer_seq;    /* observer's own monotonic packet counter */
    uint16_t pdu_len;         /* from the received LL header (tiny copy)  */
    uint8_t  tx_tag;          /* transmitter class marker (e.g. derived
                                 from deliberate PDU-length asymmetry or a
                                 visible unencrypted header field)         */
    int8_t   rssi;            /* per-packet receive margin (see below)     */
    uint8_t  crc_ok;
    uint8_t  status;          /* overwrite / missed-END / ring-full /
                                 timestamp-reversal flags                  */
};
```

RSSI needs an EXPLICIT sampling path: enable RSSI measurement (RADIO
`SHORTS.ADDRESS_RSSISTART`, or trigger `TASKS_RSSISTART`) and read
`RADIO.RSSISAMPLE` in the END ISR into `rssi`. This is what lets §8 report
per-packet receive margin for BOTH distinguishable transmitter classes
(the near/far diagnostic).

Do not format text, write UART, allocate memory, or copy full payloads in the ISR. Use the highest-priority or zero-latency interrupt configuration available. During development only, drive a GPIO high on ISR entry and low on exit so an analyzer can establish worst-observed ISR duration. Disable that marker for accepted RF captures.

Maintain explicit counters for:

- ADDRESS without a corresponding END.
- END without a fresh ADDRESS.
- END arriving while the previous record is pending.
- Ring full.
- Timestamp reversal.
- Maximum ISR duration.
- CRC failure.

### RX re-arm budget (the make-or-break assumption)

The whole approach depends on the receiver being ready for the SECOND
packet of an event before it arrives. This is a HARDWARE property, not an
ISR property:

- `END_START` (the `END`→`START` shortcut) must re-arm RX **in hardware**,
  keeping the radio in RXIDLE and restarting reception without a full RX
  ramp-up. The ISR must NEVER be on the critical path for restarting RX. If
  the path goes through a full disable/ramp (~40 µs fast, ~140 µs default),
  52 µs pairs are impossible and 70 µs is marginal; from RXIDLE the restart
  is microseconds and there is ample margin at 52 µs.
- The ISR's only deadline is to read `CC[1]`/`CC[2]` before they are
  OVERWRITTEN. `CC[1]` is overwritten at the NEXT packet's `ADDRESS`, not
  exactly 52 µs after the previous `END`. Approx read deadlines at 52 µs
  tIFS: **2M ≈ 52 + 24 ≈ 76 µs; 1M ≈ 52 + 40 ≈ 92 µs**. Target an ISR well
  below 52 µs anyway (conservative), audited by the GPIO marker.

**This retention property is qualified in Q1, not Q0.** Q0 (pipeline
integrity) proves the RADIO/PPI/TIMER0 path captures and timestamps real
packets to ±1 tick, and that the ISR span sits far below the CC-overwrite
deadline above. It does NOT and CANNOT prove close-pair retention from
ambient traffic. **Q1 is the go/no-go**: with the nRF54 synthetic generator,
demonstrate (a) that `END_START` avoids a full RX ramp and (b) that synthetic
52/70 µs pairs are actually retained in symmetric geometry. This is precisely
why a passive same-channel observer can plausibly succeed where a packaged
follower did not — and it must be demonstrated, not assumed.

## 7. Hardware timestamps

Enable the nRF52832's fixed PPI routes:

- Channel 26: `RADIO.EVENTS_ADDRESS -> TIMER0.TASKS_CAPTURE[1]`.
- Channel 27: `RADIO.EVENTS_END -> TIMER0.TASKS_CAPTURE[2]`.

(These are the correct pre-programmed nRF52832 fixed-PPI channels per the
Product Specification — verified. With `CONFIG_BT=n` the controller does
not own them, but Q0 must still confirm the RESOLVED build has not claimed
TIMER0 or PPI channels 26/27 for anything else.)

Run TIMER0 at 16 MHz, giving 62.5 ns timer ticks.

For consecutive CRC-valid packets, calculate:

```text
gap_proxy = next_packet.ADDRESS - previous_packet.END
```

This is not absolute tIFS because `ADDRESS` occurs after the next packet's preamble and access address. That PHY-specific offset is fixed, however, and cancels in comparisons:

```text
delta(gap_proxy) = delta(tIFS)
```

Expected within-PHY changes:

| Test | Expected proxy shift |
|---|---:|
| SDC 150 -> 70 us | -80 us |
| Zephyr 1M 150 -> 100 us | -50 us |
| Zephyr 2M 150 -> 52 us | -98 us |
| 150 -> 150 control | Approximately 0 us |

Do not claim absolute tIFS until the fixed ADDRESS/END offsets have been calibrated. Compare only within one PHY and one packet configuration. The change between conditions is the primary measurement.

## 8. Record pairing and reporting

Classify two records as a within-event packet pair when:

- Both are CRC-valid.
- They share PHY and channel.
- The second ADDRESS follows the first END within a preregistered window, initially 40-300 us.
- No observer overflow or missed-END indication occurred between them.

Keep all raw timing records. The host analyzer reports:

- Total packets.
- CRC-good and CRC-error counts.
- Paired-gap count.
- Unpaired packets.
- Ring drops.
- Timestamp reversals.
- Gap histogram.
- Median, percentiles, and spread before and after selection.
- RSSI or another preregistered receive-margin observable for both distinguishable packet classes.
- Maximum observed ISR duration and every loss/overwrite counter.

Full direction decoding is optional, but the qualification traffic must make the two transmitters distinguishable. Prefer deliberately asymmetric PDU lengths or another visible unencrypted marker so the record proves that packets from both endpoints were retained. The physical spacing delta still does not require a complete LL direction decoder.

## 9. Qualification sequence

### Q0 - Firmware integrity

- Correct board and silicon identification.
- TIMER advances at the expected rate.
- PPI ADDRESS and END captures change on received advertising packets.
- Drop counter remains zero.
- The ISR-duration marker shows sufficient margin before the next synthetic packet.
- The toolchain version, resolved configuration, firmware hash, and FICR identity are archived.

### Q1 - Synthetic fixed-channel packet pairs

- Run 150, 100, 70, and 52 us synthetic pairs at 1M and 2M.
- Use at least two reset-isolated captures per spacing and each nRF54 DK as generator.
- Require correct sequence pairing, CRC, timestamp ordering, and loss accounting.
- Derive the final measurement uncertainty and acceptance tolerance here; do not choose it after an FSU result is visible.
- Verify that board position and transmitter identity do not materially change the timing deltas.

Q1 is the observer's physical positive control, but it is NECESSARY, NOT
SUFFICIENT: it establishes only that the raw receiver can retain synthetic
close pairs on one channel. It does NOT qualify the *connected* observer.
Q2 must then validate two-transmitter / near-far behaviour on a real
connection, and Q3 must validate the connected SDC case, before any Zephyr
(Z1/Z2) measurement is interpretable. No connected or FSU measurement is
interpretable until Q1 passes; no DUT verdict until Q0–Q3 all pass (§15).

### Q2 - Connected 150 us baseline

Use the nRF54 connection before FSU:

- 2M connection.
- At least three reset-isolated connections.
- At least 100 accepted paired gaps per connection.
- CRC-valid pairs present.
- Stable single baseline population.
- Zero record drops.
- Both endpoint packet classes present with adequate receive margin.

This proves the observer can capture ordinary paired packets.

### Q3 - SDC HCI-selected 150 -> 70 reference

Use the SDC configuration with the existing delayed FSU request:

- The synchronized central log must contain the request and successful HCI selection of 70 us.
- The observer must contain data before and after selection.
- At least 100 paired gaps in each phase.
- Zero observer drops.
- Primary gate: paired-gap median shifts by -80 us within the tolerance frozen from Q1.
- A 150 -> 150 request/control remains within the Q1-derived no-change tolerance.

Possible verdicts:

| Observation | Verdict |
|---|---|
| Baseline and post-selection pairs; shift approximately -80 us | SDC reference measurement passes |
| Baseline works; post-selection pairs disappear | Instrument qualification failed |
| Both phases captured but shift is flat | No physical SDC spacing response detected; do not reinterpret the Q1 calibration |
| Drops, CRC collapse, or insufficient pairs | Incomplete; no interpretation |

SDC remains HCI-verified/on-air-pending until this measurement passes. Do not call HCI selection alone a physical positive control. Do not proceed to Zephyr measurements until Q1-Q3 pass.

## 10. Zephyr FSU measurements

**Payoff.** If Q0–Q3 pass, this instrument resolves TWO open questions at
once, not just the open-stack one. Q3 upgrades the SDC result from
"HCI-verified / on-air-pending" to an actual on-air measurement of whether
SDC's spacing drops to 70 µs; then Z1/Z2 address the currently-open
open-Zephyr physical gate directly (the +5.72% throughput and
−102/−103 µs completion-gap evidence). A successful chain answers the
physical question for BOTH controllers with one independent instrument.

### Z1 - 1M mechanism geometry

- Use the existing 1M/53.75 ms radio-responsive configuration.
- Compare 150 us with HCI-selected 100 us.
- Expected observer shift: -50 us.
- Use at least three reset-isolated connections per condition.
- This directly addresses the existing +5.72% throughput and completion-gap evidence.

### Z2 - 2M flagship geometry

- Use 2M/7.5 ms.
- Compare 150 us with HCI-selected 52 us.
- Expected observer shift: -98 us.
- Use at least three reset-isolated connections.
- This addresses the currently open flagship physical gate.

### Z3 - Matched SDC comparison

- Both controllers select the common 70 us floor.
- Hold PHY, interval, payload, and observer configuration constant.
- Report each controller's measured 150 -> 70 change separately.
- Do not infer controller superiority solely from timing equality.

## 11. Controls

Every campaign includes:

- Observer running with capture enabled.
- Observer powered but capture disabled, confirming it does not affect DUT behavior.
- 150 -> 150 no-change control.
- Synthetic 150/100/70/52 calibration from Q1.
- SDC HCI-selected 150 -> 70 reference measurement.
- At least three reset-isolated connections.
- DUT HCI logs synchronized with observer capture.
- Observer firmware/config hash and evidence manifest.
- Physical-board and central/peripheral role counterbalancing.

### RF geometry

Short-spacing capture can fail through a near/far effect: the observer may recover from a strong first transmitter but miss a weaker second transmitter. Therefore:

- Set equal TX power on both nRF54 boards.
- Place the observer approximately equidistant from both antennas.
- Do not place it directly against either DK.
- Record distance, orientation, TX power, and receive margin.
- Make the two directions visibly distinguishable by PDU length or another preregistered marker.
- Swap the physical central/peripheral boards and repeat.
- Repeat one accepted configuration after moving or rotating the observer.

Any material dependence on board identity, role, or modest placement changes must be reported and resolved before a physical-spacing claim.

### Transition correlation

Do not classify the exact FSU transition using unrelated UART clocks. Use one of these methods, in preference order:

1. A DUT GPIO marker wired to a PCA10040 GPIOTE input and captured into the observer TIMER when HCI completion is reported.
2. A host-issued `MARK` command to the observer after HCI completion, followed by a wide discarded transition guard band.
3. Fixed campaign phases: at least 10 s baseline, request, at least 5 s discarded, then at least 20 s post-selection.

The claim concerns steady-state spacing, so a documented guard band is sufficient if a hardware marker is unavailable.

## 12. Main risks and mitigations

| Risk | Mitigation |
|---|---|
| SDC is mistakenly treated as independently known physical truth | Synthetic hardware-scheduled packet pairs are the observer's first positive control |
| Raw receiver cannot retain 70/52 us pairs | Run Q1 before connection discovery; stop immediately if it fails |
| `CONNECT_IND` occurs on channel 38 or 39 | Bench peripheral advertises on channel 37 only |
| Connection hopping yields sparse samples | Freeze a two-channel map, park on one enabled channel, and run longer |
| Channel-map update removes the parked channel | Reject map-changing runs; keep the bench map fixed |
| ADDRESS/END offsets are not exact air boundaries | Use within-PHY deltas and calibrate with synthetic bursts |
| ISR fails to save CC registers before overwrite | Zero-latency/highest-priority ISR, tiny fixed record, GPIO timing audit, and loss counters |
| Shared RX buffer is overwritten | Avoid payload copies initially; save only timestamps/CRC/status and validate at maximum rate |
| Silicon erratum affects END events | Read FICR variant and apply documented workaround or reject the board |
| Logging perturbs reception | Freeze first; format and transmit afterward |
| Near/far behavior hides one transmitter | Equal power, symmetric placement, per-class receive margin, and role/board swap |
| Same-vendor observer shares a radio peculiarity | Synthetic calibration, SDC reference measurement, and explicit limitation |
| UART clocks misclassify the transition | Hardware marker or wide preregistered guard window |
| Post-FSU packets disappear | Treat as instrument failure, never as a DUT null |
| Observer captures unrelated traffic | Filter by access address and CRCInit |
| TIMER wraps during a long capture | Use modular 32-bit subtraction and test wrap handling before the bench |

## 13. Deliverables

Create:

```text
pca10040-radio-observer/
|-- README.md
|-- CMakeLists.txt
|-- prj.conf
|-- src/
|   |-- main.c
|   |-- radio_observer.c
|   `-- capture_ring.c
|-- tools/
|   |-- configure_observer.py
|   `-- analyze_gaps.py
`-- docs/
    `-- PREREG.md
```

Evidence bundle per run:

```text
observer binary and config
observer raw records
synthetic-generator binary, config, and self-timestamps
central log
peripheral log
analysis output
sha256 manifest
bench-layout note
```

## 14. Timebox

- Silicon/timer/PPI/ISR audit: 2-4 hours.
- Synthetic raw generator and Q1 calibration: 4-8 hours.
- Raw advertising/CONNECT_IND capture: 4-6 hours, only after Q1 passes.
- Fixed-channel connected capture: 4-8 hours.
- Ring hardening and analyzer: 4-8 hours.
- SDC qualification: 2-3 hours.
- Zephyr Z1/Z2 campaign: 3-4 hours.

Expected total if Q1 passes: approximately two to three focused engineering days plus one bench session. A Q1 failure should terminate the effort within the first day.

## 15. Stop rules

Retire or redesign the PCA10040 observer before any DUT interpretation if any of these occurs:

- Synthetic 70 or 52 us packet pairs cannot be retained reliably.
- ISR overwrite/drop accounting cannot be made trustworthy.
- Both transmitter classes cannot be received in symmetric geometry.
- ADDRESS/END delta populations do not track the synthetic generator.
- Results depend materially on board placement, board identity, or role.
- Connection/channel acquisition cannot be reproduced across three connections.
- Post-selection packets disappear or acceptance gates otherwise fail.

Proceed to a DUT physical-spacing verdict only if:

1. Q0 establishes firmware, timer, PPI, ISR, and silicon integrity.
2. Q1 measures the hardware-scheduled synthetic timing changes.
3. Q2 captures both endpoint classes at the connected 150 us baseline.
4. Q3 produces an accepted SDC HCI-selected reference measurement.
5. All loss, overwrite, CRC, receive-margin, perturbation, and synchronization gates pass.

## 16. Hardware references

- [nRF52832 Product Specification - RADIO](https://docs.nordicsemi.com/r/bundle/ps_nrf52832/page/radio.html)
- [nRF52832 Product Specification - PPI](https://docs.nordicsemi.com/r/bundle/ps_nrf52832/page/ppi.html)
