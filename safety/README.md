# BLE safety-link dead-man's-switch (reference design)

The **fail-safe component** of this benchmark's teleop use case (see the top-level
[README](../README.md)): using **Bluetooth LE as a Wi-Fi-failover safety/stop link** for robotics —
a periodic **heartbeat** whose loss asserts a physical **STOP**, with the correctness a safety path
actually needs and **physically measured** STOP latency. This is a *reference design + BLE
safety-lifecycle gotchas*, not a novel technique; it lives here (rather than a standalone repo)
because it's the STOP-signal half of the same teleop safety link the throughput/latency/soak
evidence characterizes.

## The design
```
operator ──BLE heartbeat──▶ robot MCU ──▶ [external watchdog IC] ──▶ STOP line ──▶ actuator
                              (renews the WDT only while heartbeats arrive valid & on-time)
```
- **`firmware/z52-ext-wdt/`** — the on-MCU side: it renews a watchdog **only** while valid, correctly-
  spaced heartbeats arrive; on loss/expiry it drives a STOP output.
- **`actuator/watchdog.{c,h}`** — the actuator-side safety watchdog, with the hard-won lifecycle
  correctness:
  - **generation-gated outstanding-op** — Zephyr can drop the write-completion callback on bearer
    teardown, so the outstanding gate is tagged with a connection *generation* that rides immutably in
    the callback `user_data`; a late old-generation callback can't clear a new connection's gate.
  - **expiry-race spinlock** — arming→running and the expiry ISR share one spinlock; **expiry always
    wins** (a transition re-observes STOPPED under lock and refuses).
  - **anti-burst arming** — requires N *spaced* heartbeats + a dwell, so a queued burst can't arm.

## Measured (not assumed)
Physical STOP latency on a logic analyzer: **~200 ms** last-valid-HB→STOP, **~1.4 µs** expiry→STOP;
power-loss fail-safe verified with a stand-in pull-down (a cross-board pull, not the final discrete
resistor). Design and full measurement: [`STOP-MEASUREMENT.md`](STOP-MEASUREMENT.md).

## Fail-safe engineering notes (read before trusting it)
- **A STOP pin floats HIGH (hi-Z) through reset/reboot (~400 ms)** → a **discrete external pull-down is
  mandatory and must be *measured*** (crash-vs-power-loss is indistinguishable without it).
- **An external WDT watching a *firmware-generated* heartbeat line is fail-*silent* coverage only** —
  corruption that keeps toggling the line won't trip it. Production wants a **dumb watchdog IC** (no
  firmware to trust) plus a STOP-combining circuit.
- "Accepted for queueing" ≠ transmitted; the traces are not a safety qualification until measured with
  a logic analyzer and real external pull-downs.

## Build
Build `firmware/z52-ext-wdt` for `nrf52dk/nrf52832` (see its overlay for the STOP/heartbeat/alive pins);
fit the external pull-downs before any timing run.

## License
Apache-2.0 (see LICENSE).
