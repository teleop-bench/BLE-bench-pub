# §6.1 physical-timing qualification — FROZEN preregistration

*Recorded 2026-08-07 BEFORE any capture is recorded. Realizes plan §6.1.
This qualifies STEADY-STATE frame-spacing APPLICATION only — NOT full FSU
procedure correctness and NOT spec conformance.*

## Claim boundary (fixed before recording)
- **Instrument = calibrated RADIO-event GPIO → logic analyzer.** This is a
  PHYSICAL-TIMING PROXY, NOT an on-air measurement. It reports when the
  chip's radio hardware transitions RX→TX; it never observes the RF
  packet. The literal "on-air" claim requires a qualified sniffer and is
  OUT OF SCOPE here.
- Supported claim if PASS: "the open-Zephyr M0 controller applies the
  selected steady-state tIFS on this silicon (150 → {100,52} µs), measured
  as a hardware-timed RX→TX turnaround proxy."

## Instrument (hardware-timed, DPPI-routed — not a software ISR toggle)
- `radio-fem-two-ctrl-pins` generic FEM: `ctx-gpios` asserts on radio
  TX-active, `crx-gpios` on RX-active, both routed via GPIOTE+DPPI off the
  RADIO events (the same mechanism a real power-amp/LNA uses). Two pins +
  GND to the analyzer; the nRF54 boards talk to each other over the air
  only (NO board-to-board wiring).
- **tIFS observable:** the gap from crx-deassert (RX done) to ctx-assert
  (TX start) on the PERIPHERAL's turnaround = its transmit tIFS. Direction
  captured: peripheral RX→TX (the T_IFS_ACL_PC path FSU reduces). Central
  turnaround MAY be captured as a second direction if a third pin is free;
  if only two channels, peripheral-only is preregistered and sufficient.
- **Calibration / offset:** `ctx-settle-time-us` / `crx-settle-time-us`
  are FIXED constant offsets; they add a constant to the absolute gap but
  CANCEL in the spacing DELTA (150 vs 100 vs 52). Set both to 0 in the
  overlay so the raw edge gap is the timer value; a pre-capture SMOKE
  measures the 150 baseline and its offset empirically.

## Pre-capture verification gates (must pass before any accepted run)
1. **Pin-lands-on-boundary smoke:** at spacing 150 the measured crx→ctx
   gap = 150 ± (measured fixed offset + analyzer resolution); reject the
   instrument if the 150 baseline is not stable and reproducible.
2. **Analyzer resolution — AMENDED 2026-08-07 (instrument constraint):**
   the fx2lafw (SparkFun 8-ch) streams ALL 8 channels at the full sample
   rate over USB2 regardless of channel selection; ≥16 MHz overruns USB
   and WEDGES the device (observed). Reliable sustained capture caps at
   ≤8 MHz; this campaign uses **4 MHz** (4 MB/s, USB-safe). Resolution
   0.25 µs/sample still separates the deltas with wide margin: 150→100 =
   50 µs = 200 samples; 150→52 = 98 µs = 392 samples. The ORIGINAL ≥16 MHz
   target is NOT met — recorded honestly; per-edge uncertainty = ±0.25 µs
   (± one sample), negligible against a 50–98 µs delta. Record the actual
   rate with every capture.
3. Both DK VDD and analyzer thresholds verified vs the DK I/O level.

## Capture design (FROZEN)
- **Geometries:** (A) 1M, 150→100 µs; (B) 2M, 150→52 µs.
- **Per arm, per geometry:** ≥ 5 captured connection events at steady
  state, from ≥ 2 independent connections (fresh reset between) — so a
  single anomalous event cannot carry the result.
- **Post-transition settle window:** discard the first 2 s after the
  `FSU: updated` HCI line (adoption + any transient); capture only steady
  state ≥ 2 s post-update. The 150 baseline is captured pre-request.
- **Estimand:** median crx→ctx gap (µs) over the captured events per arm;
  report median, min, max, and n.
- **Uncertainty:** reported = analyzer edge resolution + the smoke-measured
  fixed offset; the DELTA (baseline − reduced) is the primary number and
  is offset-independent.

## Accept / reject rules (FROZEN, per captured run)
- REJECT a run if: no `FSU: updated status=0x00 spacing=<target>` for the
  reduced arm; any disconnect/timeout during the captured window; analyzer
  channel dropout / missing edges in a captured event; < 5 clean events.
- REJECT the geometry if the 150 baseline smoke (gate 1) did not pass.
- Rejected captures are quarantined (`REJECTED-…`) with hash + reason and
  NEVER analyzed; an explicit accepted-run manifest gates analysis.

## PASS criterion (per geometry)
- The measured steady-state gap DELTA matches the negotiated reduction
  within (analyzer resolution + offset uncertainty):
  1M: 150→100 delta ≈ 50 µs; 2M: 150→52 delta ≈ 98 µs.
- PASS supports ONLY the steady-state-application claim above. It does NOT
  upgrade any throughput/latency/duplex number; it lets the existing 1M
  +5.72% result be reported as physically-corroborated (plan §6.2, with
  that result's own registered caveats intact).

## Outcomes (symmetric — not designed for a positive result)
- Delta matches → steady-state application confirmed (proxy level).
- Delta absent / partial → the controller does NOT apply the reduced tIFS
  at the radio despite HCI confirmation (a real, reportable negative —
  would contradict the indirect physics result and demand diagnosis).
- Delta present but off-model → investigate offset/resolution/direction
  before interpreting.

## Deliverables
- DT overlay + Kconfig (bench-only, labeled) enabling the two-ctrl-pin FEM
  on analyzer-reachable pins; committed with the capture bundle.
- Raw analyzer captures + this PREREG + accepted-run manifest + PROVENANCE
  + sha256, under this directory.
