# Stage 3 — Duplex FSU-vs-interval: methodology & traps (pre-flight)

Duplex is the messiest axis. This doc pins the design BEFORE the run so we don't hand ourselves wrong
numbers (again). Applies the session's hard-won lessons to the two-way case.

## The 5 traps duplex adds (beyond the one-way sweep)

1. **Auto-update-OFF sink (same lesson, still applies).** A duplex sink with
   `BT_GAP_AUTO_UPDATE_CONN_PARAMS=y` reverts FSU to 150 µs mid-run exactly like the one-way case →
   false numbers. **Built the sink with `=n`** (this dir's firmware). Verify held per direction.

2. **The reconnect / uplink-stall WEDGE — the big duplex-specific trap.** Independent CoC-duplex uplink
   STALLS (downlink ~150, uplink ≈ 0) on the **first CoC since the *central* booted**, at 25 ms+
   (~10/12 cold-reset), but **0/12 with a periph-only reset** (central stays booted). Our harnesses
   reflash+reset the central every cell → they would *trigger the wedge* at longer intervals and pollute
   the curve. **Mitigations (do at least one):** (a) a warm-up connection before measuring; (b) reset the
   *peripheral only* between reps where firmware allows; (c) run enough reps + a **duplex-aware
   `verify_run`** that REJECTS a stalled rep (one direction ≈ 0), so stalls are excluded and their *rate*
   is reported, not averaged in. 7.5 ms does NOT wedge (balances 12/12) — expect a clean low-interval end.

3. **Measure BOTH directions; a "balanced" duplex is ~77+77, NOT ~90+90.** Aggregate =
   downlink (sink `SINK rx … cum_total`) + uplink (central `CENRX`/its rx line). A healthy balanced link
   drops each direction to ~77 to *share* the airtime (~155 aggregate), it does not do 90+90=180. So do
   NOT gate validity on "downlink>100"; gate on "both directions >0 and steady" (real-stall vs balanced,
   per LESSONS §A).

4. **GATT-duplex is an ECHO rig — treat separately or exclude.** GATT "duplex" = an echo peripheral, so
   the reverse stream is the forward stream echoed → directions are *coupled*, not independent. The old
   "duplex FSU +14% / symmetry ≤0.1%" was an **echo-rig artifact (retracted)**. For a real duplex-FSU
   number use **independent CoC-duplex** (this dir); GATT-duplex only with the non-echo rig, clearly labelled.

5. **`verify_run` needs a duplex mode.** Extend it to parse both directions and require BOTH held (no
   step-down) + neither stalled. Until then, gate CoC-duplex manually on per-direction held + non-zero.

## Design (CoC-duplex-open, this dir)
- Firmware: `coc-duplex-central` `open-fsu`/`open-nofsu` @ units 6/12/20/30/40 (7.5–50 ms) + `coc-duplex-sink`
  `open-fsu` **auto-update-OFF** (built to `/tmp/dupsweep`, persisted here with SHA256 at run time).
- Metric per cell: **downlink KB/s + uplink KB/s + aggregate**, FSU on/off delta on the aggregate,
  counterbalanced, held-verified per direction, stalled reps rejected + stall-rate reported.
- Expected shape (hypothesis, to be measured): FSU-duplex gain is *not* the same as one-way; prior
  (contaminated) data hinted it grows with interval at 25–50 ms, but duplex saturates airtime both ways
  so headroom is smaller. **State it as measured, with the stall-rate, not as a single headline %.**

## Scope note
CoC-duplex-open is the clean first cell. CoC-duplex-SDC (NCS builds) and GATT-duplex (independent rig)
are follow-ons. Do NOT publish a duplex FSU % without the per-direction breakdown + stall-rate.
