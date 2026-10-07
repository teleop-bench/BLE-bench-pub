# FSU-regime event-fill investigation — 2026-08-13 (BOUNDED; corrected per review)

**Goal (user-prioritized):** recreate the FSU-favorable *event-filling* regime on the OPEN
controller at 2M/50 ms, to measure a real Zephyr-FSU throughput dividend directly
comparable to the SDC's +15% (apples-to-apples).

## Defensible result (the number was NOT obtained)

> At 2M/50 ms, the open GATT rig averaged **~29 LL transactions/event** (occasionally
> reaching **35**) and **~140 KB/s sender completions**, below the modeled full-event
> regime. A **separate** run reported successful **52 µs FSU negotiation**, but **neither
> physical 2M application nor any receiver-delivered throughput effect was established.**
> **The reason events average below maximum occupancy remains unresolved.**

## What is NOT established (corrections to the first draft — retracted)

1. **The 52 µs negotiation is not bound to the throughput capture.** `spacing=52 us`
   appears only in `logs/fsu-ON-negotiation-boot.log`. The occupancy/completion numbers
   are in a *different* log (`logs/fsu-ON-52us-blast-50ms.log`) that starts mid-session
   with no AA / session / boot tag / request / completion line. So I **cannot** claim the
   ~29-transaction run had 52 µs selected. Retracted.

2. **A ~40 ms event cap is NOT demonstrated.** `avg_pe × modeled pair-time` does not
   measure event duration or its close reason. And the FSU-off run itself **reaches 35
   transactions in some events** — the alleged full-interval capacity — so a hard cap at
   ~29 / ~40 ms is *contradicted by the same evidence*. Retracted. Proving a time cap
   requires event **start/end duration**, **close reason**, and **controller-queue-nonempty
   state at closure** — none of which we captured.

3. **FORCE_MD was not a valid intervention.** `fmd_arm=0` in the blast run means the tested
   steady-state path **never exercised FORCE_MD**, so it says nothing about whether
   FORCE_MD can increase event length. Separately, another archived trace shows
   `fmd_arm=2 / max=41`, which **conflicts** with "never armed." The counter lifecycle is
   unresolved. Any FORCE_MD test must first show **arm/use > 0** and reconcile the counter.
   Retracted.

4. **The FSU-off/on comparison is not a measurement.** Only central **sender-completion**
   logs are archived — no paired **sink cumulative-byte** logs (receiver-delivered
   goodput). Runs are not reset-isolated or counterbalanced, and exact flashed HEX/ELF +
   resolved configs (including the **peripheral's** 52 µs setting) are absent. Bounded
   statement: *in separate diagnostic runs, sender completion rate and occupancy appeared
   similar; no receiver-delivered FSU dividend was measured.*

5. **The observer result is NOT apples-to-apples with this arm.** Q3 proved
   **1M, 150→100 µs** physical application. This experiment is **2M, 150→52 µs**.
   `open-fsu-benchmark-problem-statement.md` correctly states 2M physical application
   remains **unverified**. Do **not** claim open and SDC reduce tIFS "identically" — that
   is unsupported at 2M.

## What a valid FSU-regime measurement requires (not done here)

- Controller instrumentation for **event start/end timestamp, close reason, and
  queue-nonempty-at-close** — to establish *why* occupancy averages < max (the actual
  open question), rather than inferring a cap.
- A **paired sink cumulative-byte** log alongside the central, **reset-isolated and
  counterbalanced** FSU-off vs FSU-on runs, with the FSU state (request + `updated`
  spacing) **in the same capture** as the throughput.
- Exact flashed **HEX/ELF + resolved .config for both boards** (incl. periph 52 µs).
- **On-air observer at 2M** to confirm the 150→52 µs physical application (the goodput
  rig cannot see tIFS; Q3 only covers 1M/100 µs).

## Files
- `logs/` — FSU-off blast, FSU-on-52µs blast (unlinked to negotiation), the boot
  negotiation trace (`spacing=52 us`), FORCE_MD-arm4 CoC run. **These are diagnostic
  captures, not a paired/reset-isolated measurement.**
- `configs/` — central blast+FORCE_MD prj.conf, central FSU-on prj.conf, `fsu-open.conf`.
