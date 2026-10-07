# Physical STOP on heartbeat loss — actuator watchdog design and measurement

A demonstration use case for the benchmark's safety link: if the BLE heartbeat stops, an actuator-side
watchdog drives a physical STOP output. This page records the design and the first hardware
measurement (logic analyzer). Firmware: the peripheral watchdog is `src/watchdog.c` in
[`apps/nrf54l15/z54-lat-periph`](../apps/nrf54l15/z54-lat-periph/) (default-off
`CONFIG_APP_SAFETY_WATCHDOG`, enabled via `bench.conf`), the heartbeat sender is in
[`apps/nrf54l15/z54-lat-central`](../apps/nrf54l15/z54-lat-central/) (`CONFIG_APP_HB_SENDER`), and the
chip-external watchdog demonstrator is [`firmware/z52-ext-wdt/`](firmware/z52-ext-wdt/). Raw logic-analyzer
captures: `debug-evidence/wdt-stop-run{3,4,5-hwwdt,6-extwdt}-20260805.sr`.

**Headline (n=4 captures, three timer implementations; only the RTOS timer was repeated):**
last-valid-heartbeat → physical STOP **200.091–200.161 ms** against a 200 ms deadline; watchdog
expiry → STOP output **1.12–1.50 µs**; power-loss fail-safe verified with a stand-in pull-down. This is a
bench demonstration, not a certified safety function.

## 1. Design

**Why the benchmark's RTT probe cannot drive it:** the serialized ping/pong probe waits up to 200 ms for each pong
before sending the next ping — so a single lost *pong* creates a ~200 ms gap in *pings arriving at
the peripheral*, and a 200 ms peripheral watchdog could assert STOP even though central→peripheral
delivery is healthy. Heartbeats must be decoupled from acknowledgements.

**Architecture:**
- Central sends heartbeats **periodically (e.g. every 20 ms), independently of acks**;
  acknowledgements are tracked separately for central-side diagnostics only.
- The actuator watchdog resets **only** on a *valid* heartbeat: dedicated GATT handle, correct
  length, **fresh monotonic sequence number**. Qualification: the sequence check rejects
  *accidental* duplicates/out-of-order frames within one session — it is **not cryptographic
  replay protection** (a malicious sender can simply choose a higher number). Resisting an active
  attacker requires the heartbeat to be **authenticated and bound to a fresh session nonce**.
  Arbitrary GATT traffic must not feed the watchdog.
- Initial deadline **200 ms ≈ 10 missed heartbeats** — finalized only after degraded-RF and
  physical-actuation measurements.
- **Fail-safe electrically:** STOP asserted through reset/boot/brownout (pull direction chosen so
  the de-energized/unprogrammed state is STOP).
- **Latched:** once asserted, STOP stays asserted; BLE reconnection alone must not resume motion —
  an explicit, validated re-arm is required.
- Deadline driven by an **independent timer/RTC or hardware-watchdog path**, not a low-priority
  BLE/application work item.

**Instrumentation (logic analyzer):** six signals + common ground (7 leads):
fault-injection edge · central accepted-for-queueing HB · peripheral valid-HB receipt ·
watchdog-expiry entry · **physical STOP output edge** (actuator-*motion* stopping remains
unmeasured) · central alive/power indicator. **Sample rate ≥1 MHz (preferably 5–10 MHz sustained)**
— a few kHz resolves the ~200 ms deadline but not expiry→STOP (5 kHz = 200 µs/sample; the
expiry-entry→STOP-output interval is exactly why MHz-rate capture matters). External pull-downs on
STOP and alive are separate physical requirements.

**Electrical pre-checks before the first timing run:** peripheral unpowered/reset → STOP low;
powered-but-unarmed → STOP low; RUNNING → STOP high; central power loss → alive falls cleanly (no
float); analyzer voltage thresholds compatible with the DK I/O voltage; overlay pins verified
against the board schematic for conflicts.

**Report three distinct measurements:** `last-valid-HB → STOP` (expect ≈ the configured 200 ms) ·
`fault-edge → STOP` (may exceed it — a heartbeat queued before suppression can arrive after the
fault edge) · `expiry-entry → STOP-output` (the MHz-rate number).

**Fault matrix to test:** central power loss · RF removal/jamming · frozen heartbeat task ·
BLE/controller wedge · peripheral reset/brownout · stale/replayed heartbeat · reconnection
(re-arm behavior). **Report `last-valid-heartbeat→STOP` and `fault-edge→STOP` separately.**

**Order:** (1) ~~inject one artificial first-attempt advertising failure~~ — **done**, retry
branch exercised and passed; (2) implement + measure this watchdog —
**firmware DRAFTED (2026-08-03, compile-verified, not yet flashed/measured):**
peripheral `src/watchdog.c` behind default-off `CONFIG_APP_SAFETY_WATCHDOG`
(STOP asserted before BT init; latched; `GPIO_ACTIVE_LOW` STOP with external-pull-down fail-safe
convention; physical-button (sw0) re-arm → ARMING → `CONFIG_APP_WDT_ARM_HEARTBEATS`=10 consecutive
valid heartbeats → RUNNING; dedicated GATT heartbeat characteristic 0x12340012 is the only feed
path; epoch+serial-arithmetic sequence validation — a central reboot *normally* produces a new epoch
(32-bit probabilistic, not a guarantee) and cannot silently re-arm; **rev 3: epoch capture is
enabled only by the physical button (`epoch_locked`)** — an invalid frame resets the consecutive
count but can never re-open capture, so a second sender cannot arm without a new button press;
k_timer deadline **explicitly draft-only** — production needs an independent RTC/HW-watchdog
path; LA pins: hbrx toggle per valid HB, **expiry toggle at callback entry before the STOP write**,
STOP level). Central heartbeat sender behind default-off
`CONFIG_APP_HB_SENDER` (fixed 20 ms period on absolute timepoints from a thread, decoupled from
ping/pong; **rev 2 after code review:** late wake-ups SKIP missed periods — never a catch-up
burst; **at most one outstanding heartbeat**, gated on the `bt_gatt_write_without_response_cb`
completion callback ("accepted" ≠ transmitted), with `attempted/queued/completed/dropped/missed/aborted` counters — **rev 4: the outstanding gate
is generation-tagged and freed on disconnect** — Zephyr may drop the completion callback on bearer
teardown, so the disconnect path (generation bump + gate clear + conn clear, all under one mutex)
frees it; the operation's generation rides **immutably in the callback's `user_data`** (a shared
global would be overwritten by the next op — the reviewed ABA race), the sender captures
{conn ref, generation} as a pair under the same mutex, and `completed` increments only when the
callback itself clears the gate. With that — and **rev 5: the compare+clear itself is serialized under the same mutex** (as
separate atomics, an old callback could pass the compare, get preempted across a
disconnect + reconnect, then clear the new gate; sender acquisition of
{connection, generation, outstanding} is likewise one locked step, and the API-error cleanup does
the same locked compare-and-clear) — a late old-generation callback cannot clear a new
connection's gate and the gate lifecycle proof is complete; the TX marker
toggles only on *accepted-for-queueing* (inside the success branch); central GPIO bring-up is
checked and a failure disables the sender (a measurement build must not pretend to measure); sw0
suppression is **one-way until reboot** (debounce-proof, single fault edge); runtime STOP-write
failures on the peripheral latch `hw_fault` and release the pin to hi-Z so the external pull
asserts STOP; expiry logging moved outside the spinlock; per-submission counted conn-reference under a mutex — no raw `default_conn` use (ping
path fixed identically); sw0 *suppresses sending* (task stays alive — labeled accurately) and
toggles a dedicated fault-edge marker pin; `alive` pin distinguishes suppressed-sending from power
loss and needs an external pull-down to fall cleanly; dedicated-char discovery chained after
subscribe). **Peripheral rev-2 fixes:** all state transitions + safety-GPIO writes under one
**k_spinlock shared with the expiry ISR** — expiry always wins the ARMING→RUNNING race; arming
requires **N spaced heartbeats (≥10 ms apart) AND a ≥180 ms ARMING dwell** — a burst of queued
frames cannot arm; the expiry LA marker is a dedicated toggle at callback entry **before** the STOP
write (independent marker, no negative expiry→STOP artifact); GPIO bring-up is fully
error-checked — failure latches STOP, blocks Bluetooth startup, and makes RUNNING unreachable;
write-envelope validation (offset=0, no prepared writes); Kconfig ranges forbid zero
period/timeout/count; epoch wording corrected to "normally produces" (32-bit probabilistic).
DT overlays carry **bench-placeholder pins with verify-before-wiring warnings**; measurement waits
on the logic analyzer (Mouser TOL-18627 + PRT-11026, ordered); (3) degraded-RF
across 1 M / 2 M / Coded S=2 / Coded S=8; (4) supervision-timeout sweep under degraded RF.

## 2. Hardware measurement (2026-08-05)

Bench brought up and the three §1 measurements captured on the first clean run
(TOL-18627 @ 8 MHz, 6 channels, trigger = fault edge, 40% pre-trigger, n=1).

**Results (runs 3 & 4 = the two clean triggered runs; runs 1–2 were LA-arming
misses, system behavior nominal):**

| Measurement | Run 3 | Run 4 |
|---|---|---|
| last-valid-HB → physical STOP fall | **200.104 ms** | **200.091 ms** |
| fault-injection edge → physical STOP fall | 196.278 ms (fault 3.826 ms after last HB) | 200.138 ms (one in-flight HB accepted 47 µs *after* the fault edge) |
| expiry-callback entry → physical STOP output | **1.50 µs** | **1.38 µs** |

Deadline is CONFIG_APP_WDT_TIMEOUT_MS=200 from last valid HB: measured overshoot
**+104 / +91 µs** (k_timer granularity + ISR + GPIO write); run-to-run spread
**13 µs**. Run 4 also exhibited the §1-predicted fault→STOP case: a
pre-suppression in-flight heartbeat landed after the fault edge, correctly
extending fault→STOP past the deadline (the design measures from last *valid*
HB, and the trace shows exactly that). Additionally, the central reflash between
runs silenced heartbeats and the RUNNING watchdog latched STOP on its own
(stops counter 2→3) — an unplanned real-interruption trip, not button-injected. Capture hygiene: exactly one
fault edge (sw0 one-way suppress = debounce-proof as designed), one wdtexp edge,
one STOP fall, 20 HBRX edges in the 400 ms pre-trigger (= 50 HB/s). STOP remained
latched low after the event; ALIVE stayed high throughout — the crashed-but-powered
signature. NOTE: this demonstrates only ONE leg of the crash-vs-power-loss
distinction; the other leg (ALIVE *falling* on power loss) requires the external
pull-down, and without it ALIVE floats on power loss — the distinction is NOT yet
reliable until the pull-downs are fitted and the power-loss pre-checks run.

**Bench corrections made during bring-up (now in the tree):**
- §1 placeholder pins P1.08/09/10 collided with Button2/Button1/LED1 on the
  nRF54L15-DK; P1.15/16 exist on the SoC but are NOT routed to the DK headers.
  Final pins (both boards): **P1.11** (alive / stop), **P1.12** (hbtx / wdtexp),
  **P2.06** (fault / hbrx). Overlays updated with the verified port-1/port-2 claim maps.
- `CONFIG_APP_HB_SENDER` / `CONFIG_APP_SAFETY_WATCHDOG` default `n` and were never
  enabled — prior "builds clean" checks had compiled neither subsystem (and the old
  build dirs predated the overlays entirely). Enabled via per-app `bench.conf`
  fragments (`west build -- -DEXTRA_CONF_FILE=bench.conf`), NOT `prj.conf`, so
  ordinary latency builds stay unchanged — enabling the HB sender visibly shifts
  the ping RTT distribution (observed mean ~11.9 → ~8.9 ms), so bench and
  latency-baseline configurations must not be mixed.
- LA channel map: CH0 alive, CH1 fault, CH2 hbtx (central); CH3 hbrx, CH4 wdtexp,
  CH5 stop (periph); common ground central↔periph↔LA.
- Input-threshold pre-check passed empirically (driven highs read solid at the
  FX2 inputs; VIH 2.0 V).

**Power-loss electrical checks (2026-08-05, later same session) — BOTH PASSED**
using a cross-board pull-down stand-in (no discrete resistors on hand): the
*other* board's spare pin P2.08, configured input + internal ~13 kΩ pull-down
and jumpered to the DUT line, serves as both the external pull-down and a
1 Hz level monitor (`mon=` in the surviving board's stats line; `mon-gpios`
in both overlays, diagnostic-only bring-up):
- **Unpowered watchdog → STOP asserts:** periph armed (RUNNING, STOP high,
  mon=1) → periph power switch OFF → **mon=0** — the dead board's STOP line
  fell to asserted under the passive pull alone, and stayed there. Power back
  ON → boots latched-STOP, still mon=0 (reboot never re-enters RUN).
- **Central power loss → ALIVE falls:** central powered (mon=1) → central
  power OFF → **mon=0**. Crash-vs-power-loss is now distinguishable as
  claimed: crashed-but-powered = ALIVE high + HB silence (run 3/4 traces);
  unplugged = ALIVE low (this check).

CAVEAT: internal-pull stand-in (~11–16 kΩ, in silicon of a powered helper
board) — proves polarity + concept; the production actuator circuit must
re-prove with its own discrete resistor and load. A 1 Hz sampled level check
is the intended §1 semantics here (settle-and-stay, not edge timing).

**Rev 4 — deadline moved to the ON-CHIP HARDWARE WATCHDOG (same day, run 5):**
wdt31 (32.768 kHz LFCLK), installed at early init, countdown started at ARMING
entry, fed per valid heartbeat; expiry = pre-reset interrupt (marker + STOP
write) then CHIP RESET into the latched-STOP boot state. Measured:
**last-valid-HB → STOP 200.130 ms; expiry-entry → STOP-output 1.25 µs** —
equivalent to the RTOS-timer runs (200.091–200.130 ms across all three): no
material latency increase relative to the 200 ms deadline (the differences that
WERE measured are tens of µs). Reset-cause register confirmed the
trip post-boot (`wdt=STOPPED(posttrip)`).

**Measured pull-down motivation:** 80 µs after the STOP write the trace shows
the line float back HIGH — the reset released the pin to hi-Z (≈2 LFCLK after
the pre-reset interrupt, per spec) and NO pull-down was fitted; it floated for
the remaining ~400 ms of the capture (reboot window). The reset-into-safe
design hands STOP to the external pull-down for the entire reboot gap — this
trace is the direct evidence that the discrete resistor is mandatory, not
belt-and-suspenders.

**Rung 3 — chip-EXTERNAL watchdog demonstrated (same day, run 6):** an
nRF52-DK (PCA10040, on hand) as an independent fail-silent-coverage watchdog
(see scope note below) on separate silicon,
clock, and power; taps a dedicated HBRX mirror line (periph P2.10, one toggle
per valid HB) into P0.11; feeds its OWN hardware WDT (200 ms); drives its own
active-low STOP (P0.12, LA CH6); trips = pre-reset ISR STOP write + chip
reset; auto re-arm after 10 spaced edges + 180 ms dwell (demonstrator
simplification — the manual re-arm gate stays on the nRF54; system STOP =
either line low). nRF54 boards lowered to VDD 3.0 V (Board Configurator) to
match nRF52 I/O. Dual-trip measurement, one fault injection:
**nRF54 STOP at 200.146 ms (expiry→pin 1.12 µs), nRF52 STOP at 200.161 ms —
the two independent watchdogs fired 15 µs apart.** Both chips confirmed the
trip post-boot via reset-cause register. Scope (per review): this demonstrates
independent FAIL-SILENT/frozen-primary coverage — the external watchdog watches
a heartbeat-evidence line generated by the primary's software, so corruption
that keeps toggling that line would not trip it; the two STOP outputs were
observed on separate LA channels (no combining circuit was built); re-arm is
automatic (demonstrator). Production still wants a dumb watchdog IC (no
firmware to trust) plus the STOP-combining circuit in the actuator design.

**Still open:** repeatability (four captures across three timer implementations
— only the RTOS timer was repeated — all 200.091–200.161 ms); degraded-RF false-trip characterization; production STOP
drive circuit + discrete pull-down + purpose-built external watchdog part.
Capture files: `debug-evidence/wdt-stop-run3-20260805.sr`, `-run4-`,
`-run5-hwwdt-`, `-run6-extwdt-`.
