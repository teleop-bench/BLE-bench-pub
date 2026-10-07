# Open wireless benchmark platform — future investigation

**Status:** concept / future work  
**Current scope:** documentation of an opportunity discovered while benchmarking BLE 6.0 for robotic teleoperation  
**Not currently claimed:** a general-purpose platform, a conformance suite, a safety-certification system, or a first-of-its-kind invention

## Relationship to the current publication

The current publication should remain focused on the bounded question:

> **Can Bluetooth Low Energy 6.0 Back Up Wi-Fi Robotic Teleop? Throughput, Latency, FSU, Soak, and Physical Stop Testing.**

That work has a defined audience, test campaign, evidence set, and practical conclusion. The measurement system described here should appear there only as supporting evidence of rigor.

This document preserves a larger follow-on question:

> Can the project-specific measurement chain be generalized into an open, provenance-bound platform for testing whether wireless-controller behavior reported by software actually occurred on air, reached the receiving application, and produced the intended physical response?

Answering that question is a separate effort. It requires packaging, generalization, prior-art research, external reproduction, and maintenance that are not necessary for publishing the present BLE teleoperation results.

## Core idea

Most wireless benchmarks stop at a convenient proxy: an API returned success, a sender completed a write, a controller reported a negotiated feature, or a chart showed an application-level rate.

The current system connects several otherwise separate evidence layers:

```text
test workload and application queues
              ↓
host and controller state
              ↓
physical on-air packet timing
              ↓
receiver-delivered data
              ↓
local watchdog and physical STOP output
```

Each layer has an independent observation, and a result is accepted only when the relevant layers agree and the exact firmware, configuration, connection, tools, and raw logs are bound into the evidence.

The benchmark results are one dataset. A generalized platform would be a repeatable way to produce future trustworthy datasets.

## What exists today

### 1. Passive BLE inter-frame timing observer

[`pca10040-radio-observer/`](../../apps/nrf52/pca10040-radio-observer/) turns an nRF52 DK into a raw-radio timing observer. It does not run a BLE stack. Hardware events capture packet `ADDRESS` and `END` timestamps on a 16 MHz timer, permitting direct measurement of the previous-packet-END to next-packet-ADDRESS span.

Current capabilities include:

- Runtime binding to access address, CRCInit, channel, channel map, and PHY.
- Hardware capture rather than UART-timing inference.
- Known-gap validation at 52, 70, 100, and 150 microseconds.
- Qualification at BLE 1M and 2M.
- RSSI, CRC, loss, stale-capture, ring-overflow, and ISR diagnostics.
- Machine-readable capture lifecycle records.
- Connection-role attribution through near/far geometry and role swaps.

This is more accurately described as a **BLE physical timing instrument** than as a general packet sniffer.

### 2. Instrumented endpoints and controller

[`zephyr-patches/fsu-m0-series/`](../../zephyr-patches/fsu-m0-series/) contains a reproducible Zephyr 4.4.1/nRF54L15 experimental FSU controller series. It imports and attributes earlier Zephyr FSU reference work, then adds M0 integration fixes, responder notification fixes, effective-state behavior, diagnostics, and physical-validation instrumentation.

The bench-only `CONFIG_BT_CTLR_TIFS_CAPTURE_BENCH` instrumentation provides:

- Hardware-timestamped RX-PHYEND to response-TX-READY capture.
- A dedicated timer capture register that avoids controller-owned timing resources.
- Correct handling of the peripheral's multiple TX-completion ISR paths.
- Separate histograms for programmed 150, 100, and 52 microsecond spacing.
- Clear, freeze, drain, drop, stale, duplicate, and lifecycle accounting.
- Independent on-chip comparison with the external observer.

The series also provides endpoint-owned connection identity and per-channel counters used as non-circular observer-retention denominators.

### 3. Experiment runner and analyzers

The [`q2-central/`](../../apps/misc/q2-central/) tooling includes:

- `q2_run.py`: hardware reset, deterministic startup, flashing, capture orchestration, snapshots, archiving, and verdict ownership.
- `analyze_q2.py`: common structural, configuration-binding, retention, timing-window, and calibration validation.
- `analyze_q3.py`: within-connection 150-to-100 microsecond FSU step analysis.
- `analyze_q3_2m.py`: distinct 2M/150-to-52 microsecond analysis.
- `combine_calib.py`: provenance-bound control calibration.
- `combine_abba.py`: counterbalanced steady-state confirmation and drift cancellation.
- `assert_fsu_config.py`: semantic validation of the resolved endpoint configurations.

Important behavior includes:

- Hardware/host time anchors and conservative transition exclusion.
- Independent endpoint denominators rather than observer-visible denominators.
- Firmware snapshot before hardware mutation.
- HEX, ELF, and resolved `.config` hashing and archival.
- Flashing from the snapshotted image with post-run TOCTOU verification.
- Session, device, boot, AA, CRCInit, map, channel, and PHY binding.
- Calibration consumers that rehash and re-analyze the original evidence.
- Dirty-tree, unverified-image, wrong-arm, and contract-deviation quarantine.
- Smoke runs that can never accidentally become accepted evidence.
- Preservation of failed campaigns rather than selective cell replacement.
- Frozen contracts, reset-isolated replication, and ABBA counterbalancing.

### 4. Receiver-delivered throughput and latency-under-load measurements

The broader benchmark applications and evidence distinguish:

- Sender submission and completion.
- Receiver-delivered bytes.
- Queue depth, pool pressure, and event occupancy.
- One-way and duplex throughput.
- Heartbeat RTT distributions under offered load.
- Naive bulk submission versus completion pacing.

The latency campaign demonstrated why throughput and control latency must be measured simultaneously: a link can retain high delivered throughput while queue depth destroys the control-latency tail.

See [`debug-evidence/latency-under-load-20260813/`](../../debug-evidence/latency-under-load-20260813/) and [`debug-evidence/systematic-sweep-20260814/`](../../debug-evidence/systematic-sweep-20260814/).

### 5. Heartbeat-to-physical-STOP chain

[`z54-lat-periph/src/watchdog.c`](../../apps/nrf54l15/z54-lat-periph/src/watchdog.c) contains a bench reference implementation that connects valid heartbeat reception to a local hardware watchdog and latched STOP output. Logic-analyzer capture measures the physical output rather than inferring it from software logs.

This is evidence tooling and a reference design, not a certified safety system.

## Novelty hypothesis

The individual components are not all novel:

- BLE sniffers and raw-radio receivers already exist.
- Hardware timestamping is established practice.
- FSU is defined by the Bluetooth specification.
- The controller series imports prior Zephyr FSU work.
- Watchdogs, ABBA designs, manifests, and cryptographic hashes are established techniques.

The potentially original contribution is their integration into a low-cost, executable system that jointly proves:

1. What the software requested and reported.
2. What physically occurred on air.
3. What reached the receiving application.
4. What physical output followed a communications failure.
5. Which exact firmware, configuration, tools, and raw evidence produced the result.

The platform is intentionally **false-accept-resistant**: missing provenance, stale calibration, modified evidence, incomplete capture, unverified firmware, a dirty source tree, or a diagnostic smoke must prevent acceptance rather than merely produce a warning.

This is a novelty hypothesis, not a priority claim. A literature, open-source, commercial-tool, and patent survey would be required before using terms such as “first” or “unique.”

## Why it could be valuable

### Beyond a single benchmark

- Re-run the same evidence contract against new Zephyr, Nordic SDC, controller, SoC, and SDK releases.
- Detect behavioral regressions that unit and conformance tests do not expose.
- Separate application, host, controller, RF, receiver, and actuator failures.
- Compare open and proprietary controllers using the same open workload and measurement system.
- Turn bug reports into reproducible evidence bundles.
- Make vendor performance claims independently falsifiable.
- Preserve negative and null results with enough context to interpret them.
- Build a public dataset whose results can be recomputed from raw evidence.

### Potential users

| User | Potential value |
|---|---|
| Robotics founders and technical leaders | Independent due diligence on link capacity, latency, recovery, and backup-link claims |
| Robotics and embedded engineers | Localization of queue, host, controller, RF, delivery, and watchdog failures |
| Bluetooth controller and SDK developers | Physical validation of LL procedures and hardware-in-the-loop release regression |
| Silicon and module vendors | Reproducible performance evidence under an externally inspectable workload |
| Researchers and reviewers | Raw-data re-analysis, frozen protocols, counterbalanced replication, and negative-result preservation |
| Reliability and safety engineers | Evidence connecting communications availability to local fail-safe behavior, without treating the radio as the safety mechanism |
| Independent laboratories and consultants | A repeatable starting point for comparative wireless qualification services |

## Three foundational lessons

### 1. Never confuse software success with physical success

A callback, accepted send, or negotiated setting proves that software accepted an operation. It does not prove that radio timing changed or that the receiver obtained the data.

### 2. Validate the limiting resource before testing an optimization

An airtime optimization cannot improve throughput when the application, MTU, credits, queue, or controller event policy is the binding limit. Every experiment needs a positive preflight proving that it can expose the proposed effect.

### 3. Make a false result difficult to accept

Bind every cell to the firmware, resolved configuration, connection, raw logs, analyzer, protocol, and calibration that produced it. Preserve failures, freeze acceptance rules before accepted data, and require independent replication.

## Questions for a future investigation

### Technical generality

- Can the observer support ordinary 37-channel hopping without requiring a pinned two-channel map?
- Can connection discovery and following be automated without weakening configuration binding?
- Can the same evidence schema support non-BLE radios?
- Which metrics and gates are generic, and which belong in protocol-specific plugins?
- Can hardware timing remain non-perturbing across additional SoCs and controller architectures?
- Can synchronized interference, mobility, and RF-geometry metadata be incorporated cleanly?

### Reproducibility

- Can another developer reproduce an accepted example from a clean checkout and a documented BOM?
- Can all toolchains, SDKs, binaries, configs, raw logs, and firmware licenses be redistributed?
- Can hardware-free synthetic tests exercise every acceptance and rejection path?
- Can a machine-readable evidence schema be versioned independently of the current scripts?
- Can results remain verifiable when upstream repositories or SDK downloads disappear?

### Novelty and positioning

- What open or commercial tools already join controller state, on-air timing, delivered data, and physical-output validation?
- Is the strongest contribution the observer, the on-chip profiler, the evidence contract, or the complete system?
- Is the appropriate comparison conformance testing, RF test equipment, embedded benchmarking, or reproducible research infrastructure?
- Should the project remain BLE-specific or become a broader teleoperation-link qualification framework?

### Product and maintenance

- Who owns supported hardware, toolchain upgrades, and evidence-schema compatibility?
- Is the useful output an open-source kit, a hosted results catalog, a hardware appliance, or a qualification service?
- What minimum subset provides value without requiring users to reproduce the entire research campaign?

## Work required before calling it a platform

1. **Create a standalone repository.** Separate reusable tooling from project history and customer-facing conclusions.
2. **Define licensing and attribution.** Clearly separate original code, imported Zephyr patches, upstream controller work, Nordic HAL dependencies, and evidence redistribution rights.
3. **Specify the hardware.** Publish a BOM, board identities, wiring, antenna geometry, logic-analyzer connections, and supported host environments.
4. **Provide one-command workflows.** Build, assert, snapshot, flash, capture, analyze, and verify without undocumented shell state.
5. **Version the evidence schema.** Document logs, manifests, calibration artifacts, verdicts, and compatibility rules.
6. **Separate core and plugins.** Core orchestration/provenance should not depend on one FSU experiment; BLE/FSU analysis should be a plugin or profile.
7. **Ship reference cells.** Include one accepted example, one quarantine, and several intentional rejection cases.
8. **Add hardware-free CI.** Replay real and synthetic evidence through all analyzers and provenance gates.
9. **Perform an independent reproduction.** A second person or laboratory must reproduce a defined result without oral guidance.
10. **Conduct prior-art research.** Compare against open sniffers, commercial Bluetooth analyzers, controller qualification suites, HIL test systems, and reproducible benchmark frameworks.
11. **Document limitations.** Single-channel retention, observer capacity, UART behavior, supported PHYs, instrumentation perturbation, RF geometry, and non-certification scope must remain explicit.
12. **Establish maintenance boundaries.** Define supported boards, Zephyr/SDK versions, response expectations, and archival policy.

## Suggested maturity gates

### Gate 0 — internal research rig

- Current state.
- Produces accepted evidence for the project-specific BLE/FSU campaign.
- Requires substantial repository and hardware context.

### Gate 1 — reproducible kit

- Clean checkout and documented BOM.
- One-command accepted reference run.
- Complete archived firmware and evidence.
- Hardware-free verifier.
- Reproduced by a second developer.

### Gate 2 — reusable BLE qualification framework

- Multiple controllers and SoCs.
- Configurable PHY, connection, channel, workload, and outcome profiles.
- Stable evidence schema and plugin interface.
- Release-to-release regression campaigns.

### Gate 3 — broader wireless benchmark platform

- At least one non-BLE radio family.
- Common workload and physical-output abstractions.
- Cross-radio joint-loss and common-cause testing.
- Independent laboratory reproduction.

Only after Gate 1 should the project be publicly described as a reusable platform rather than a rigorous project-specific rig.

## Possible future outputs

- **Open hardware-in-the-loop benchmark kit:** firmware, runner, analyzers, observer, protocol profiles, and example evidence.
- **Controller regression suite:** repeat physical timing, delivery, latency-tail, reconnection, and STOP-chain tests for every release.
- **Public teleoperation-link benchmark:** comparable results across hardware, controllers, transports, and radio technologies.
- **Reproducible bug-report format:** exact firmware and evidence bundle accompanying each controller or stack defect.
- **Independent qualification service:** paid execution and reporting under an open protocol. This must not be called certification without the necessary standards, process, and accreditation.

## Boundaries

The platform would not, by itself:

- Establish Bluetooth conformance.
- Certify a functional-safety level.
- Prove field reliability from clean-bench evidence.
- Make two radios statistically independent.
- Replace a local fail-safe controller or physical emergency-stop system.
- Explain the internals of a proprietary controller solely from black-box measurements.

It could provide reproducible evidence used by the relevant engineering and assurance processes.

## Smallest useful next step

Do not generalize the tooling before publishing the bounded BLE teleoperation result.

After publication, the smallest follow-on investigation should be:

1. Extract one representative accepted BLE cell and one deliberate rejection into a minimal standalone package.
2. Write a clean-room reproduction guide that assumes no prior project knowledge.
3. Ask a second developer to reproduce both verdicts.
4. Record every undocumented dependency or manual intervention.
5. Decide whether the resulting packaging work justifies a dedicated platform repository.

This tests the platform hypothesis cheaply. If an independent developer cannot reproduce the minimal example, broader abstraction work is premature.

## Current supporting evidence

- [FSU controller series and provenance](../../zephyr-patches/fsu-m0-series/README.md)
- [Accepted Q3a on-air results](../../debug-evidence/fsu-q3a-20260812/RESULTS.md)
- [Q3 acceptance protocol](../../debug-evidence/observer-q3-20260811/Q3-ACCEPTANCE-PROTOCOL.md)
- [2M acceptance protocol](../../apps/misc/q2-central/Q3-2M-ACCEPTANCE-PROTOCOL.md)
- [Latency-under-load findings](../../debug-evidence/latency-under-load-20260813/FINDINGS.md)
- [Latency-tail hardening campaign](../../debug-evidence/latency-under-load-20260813/hardening-20260814/README.md)
- [Systematic sweep](../../debug-evidence/systematic-sweep-20260814/README.md)
- [Watchdog reference implementation](../../apps/nrf54l15/z54-lat-periph/src/watchdog.c)

## Candidate future positioning

If Gate 1 is achieved, a defensible description would be:

> **An open, provenance-bound hardware-in-the-loop measurement system for testing whether wireless-controller behavior reported by software occurred on air, reached the receiving application, and produced the intended physical response.**

Until then, the more accurate description is:

> **The open measurement chain used to produce and audit the BLE teleoperation benchmark.**
