# LESSONS — benchmark pitfalls, retractions & gotchas

**Read this before designing or interpreting a benchmark run.** This is the project's internal
working record of pitfalls and retractions, kept public for transparency; it is terse by design.
It exists so nobody (human or agent) has to sweep `debug-evidence/**` to rediscover them. One line each + where it's from. **When a
run establishes or retracts a lesson, add/flip it here** (one line + pointer); the authoritative
detail stays in the cited dir. Operational config gotchas (SDC subshell, DTR reader, EATT −6 MTU,
sanity-gates, interval pinning) live in [REPRODUCE.md](../REPRODUCE.md), not here. (The stale-`fsu` /
FSU-revert trap is not relegated — it is lesson #1 below, because it produced a wrong headline.)

## ★ #1 — Validate the measurement before you believe (or theorize on) it
The single most expensive mistake in this project, made twice. Before trusting any run:
1. **Verify the treatment is IN EFFECT for the WHOLE measurement window — not just requested/negotiated
   once.** FSU (and PHY/DLE/interval/TX-power) can silently **revert mid-run**: the peripheral's ~5 s GAP
   auto-param-update resets tIFS back to 150 µs, yet the app keeps printing the **stale latched `fsu=52`
   token**. A boot-time token is NOT proof of live state. Confirm steady-state to end-of-run (throughput
   doesn't step down; final spacing still 52). **This artifact made us report "CoC FSU ≈ 0%" when it is
   actually ≈ +20%** — the on-arm ran 188 KB/s for 4 s then reverted to 160, and a token-grep called it
   "engaged." Fix: build responders **`BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`** (or request FSU after the final
   update) and assert *held* spacing. [coc-fsu-corrected-20260908; the invalidated coc-fsu-interval /
   fsu-campaign CoC cells]
2. **A new result that contradicts your own BETTER-CONTROLLED prior does not get to overturn it until you
   can explain the difference.** Our "CoC FSU flat" contradicted `tx-staging-20260826` (auto-update OFF,
   spacing verified, pinned → +19%), which was still marked RESOLVED. The prior was right; reconciling it
   first would have found the auto-update delta immediately, instead of four wrong root-cause theories
   (RF-day → EVENTFILL_DIAG → controller-version → sink-version).
3. **Don't theorize on an unvalidated instrument.** We built an elaborate "below-ceiling RF/session"
   root-cause on data whose *measurement* was invalid. Ask "is the instrument measuring what I think?"
   before "why is the number what it is?"

## ★ #2 — Per-capture gating validates the signal, not the design
`verify_run` proves a *single* run is real (FSU held, link alive, right operating point). It is
structurally **blind to a confounded A/B**: if the on-arm and off-arm builds differ in more than the one
variable under test, **each arm still produces a perfectly clean, held signal** — and the "delta" is not
attributable to the variable. This shipped: the GATT FSU-off arm *dropped the whole FSU feature package*
(36 resolved-config symbols incl. `BT_BUF_EVT_RX_SIZE` 255→68), not just the requested spacing, so the
"GATT FSU +20%" was not pure FSU. It sat **documented-but-unenforced** in REPRODUCE.md and reached an
external report anyway. **Rule: a pitfall isn't "captured" until it's a gate that fails the run, not a
paragraph.** Enforce the experimental design, not just the signal:
- **Matched-pair config diff** (`tools/check_matched_pair.py`): the two arms' *resolved* `.config` must
  differ ONLY in the whitelisted variable; any other differing symbol → refuse to run. (Watch the path:
  sysbuild nests the app config under `<build>/<app>/zephyr/.config` — the top-level one is empty of BT
  symbols and will false-"MATCH" two blanks; the linter now hard-errors when the variable is absent in both.)
- **Responder capability + auto-update-OFF on the *sink*** must be asserted at build (a low-floor central
  with a default-150 responder silently *understates* the on-arm; a sink without auto-update-OFF reverts).
- **Flip operating-point checks from warn→reject** (PHY=2M, DLE=251, interval==requested, version uniform
  across the cell's firmware). Every prose caveat in this file is a check we haven't written yet.
[gatt-fsu-clean-20260908; the superseded fsu-interval-sweep / sdc-fsu-interval-sweep GATT cells]
- **It recurred after the fix, in the "corrected" campaigns.** The GATT cells of `coc-fsu-corrected-20260908`
  and `sdc-fsu-corrected-20260908` (and the `caps-gatt442` re-run) used off-arm hexes with **no FSU request
  code at all** — checked by grepping the off-arm firmware, which `verify_run` never does. Their numbers
  happen to agree with the matched `gatt-fsu-clean` sweep at 7.5 ms (+20.0/+25.1% vs +19.8/+24.6%), but
  agreement is luck, not design: cite `gatt-fsu-clean`. **Check every archived arm's *firmware*, not just
  its logs — and never pair cells from different intervals/trees as a cross-transport "same band."**
  [EVIDENCE-INDEX rows for both corrected campaigns, 2026-10-02]

- **The observer tooling is hash-bound: don't edit it casually.** `combine_abba*.py` re-hashes the
  analyzers, runner, combiner, asserter and protocols *now* and rejects any archived cell whose recorded
  digests differ ("current lineage"). Editing even a print label in those files breaks re-verification
  of every accepted cell. The 1M bundle can already no longer be re-combined, and a fresh 1M run fails
  analysis because the frozen 1M calibration is bound to the pre-2M-port `analyze_q2.py` (tooling
  changed in the 2M port; a 1M re-run needs a new calibration campaign). Treat them as frozen; fix wording in docs instead. Likewise, a path scrub changes bytes that
  combiners re-hash: re-verify on a `tools/scrub-paths.py unscrub-tree` copy.
  [fsu-q3a-20260812 / q3-2m-accept-20260906 rows, 2026-10-02]

## A0. Field / degraded-RF test lessons (from the range walk, 2026-09-09)
- **Record the RF environment — metal dominates.** A large metal object in the path (plus 3 walls) made the
  degraded-RF distances *pessimistic*; metal = strong attenuation + multipath, far worse than drywall. Absolute
  feet do NOT transfer; the *shape/behavior* does. Always log obstructions (walls, metal, floors). [rf-range-safety-20260909]
- **The degraded-RF failure mode is latency-first, then a clean disconnect — and that's SAFE.** As RF degrades,
  LL retransmits inflate the stop-signal RTT (10 ms → 100s of ms) with 0 loss first; then a ragged marginal band
  with sporadic loss; then a supervision-timeout disconnect (0x08). The link never delivers *silent stale* data —
  loss-of-link is itself a STOP (dead-man's-switch). Report the range as solid/marginal/dropped bands, not one number.
- **Field-rig design: put all variability on the TETHERED board; the far board is a dumb echo, flashed once.** The
  central (on the Mac) is re-flashable freely to change interval/PHY/load; the moved peripheral just echoes and never
  comes back. BUT a CoC↔GATT (or open↔SDC) swap needs the *far* board re-flashed, which you can't do at range — so
  either pick the transport before the walk, or build a dual-transport peripheral (one echo answering both). [rf-range-safety-20260909]
- **Opening a new serial capture RESETS the nRF54 (DTR-on-open) — keep ONE continuous capture per walk.**
  *(2026-10-03: not reproduced with `tools/capture-tool.py` — opening an nRF54L15-DK port showed no boot
  line across dozens of reps, while the nRF52 DK does reset on open. Likely reader/host-dependent: check
  for a boot line on your rig before relying on either behaviour.)* Re-arming
  the capture mid-walk resets the central, drops the link, and loses the transient you were trying to catch (it cost
  us the exact loss-onset distance between 40–80 ft). Start one long capture and poll the file; never restart it mid-run.

## A1. Rig and tooling traps found 2026-10-02/03
- **`nrfutil device program` leaves the core halted.** The new image doesn't run (silent console, no
  advertising/connection) until `nrfutil device reset`; `west flash` resets for you, which hides this.
  Always reset after programming. [tools/coc-duplex-fsu.py, onair-duplex.py]
- **An FSU-off arm that requests 150 µs on a 150 µs link is a no-op, so the open controller sends no
  completion event** (no GATT `FSU: updated`, no CoC `Q3FSU-DONE`); SDC does report `spacing=150`. Time the
  FSU-off window from the request line and never require a completion event on that arm.
  [gatt-duplex-fsu-matched-20261003]
- **Gates written against assumed log formats must be run on hardware before they're trusted.** The
  archival runner's gates (added 09-24) had never met a board: `nrfutil` 8.x prints board fields unindented
  (detection found 0 boards), the GATT firmware never logs data length (every GATT cell aborted), and a
  recovered RF dip was treated as an FSU revert (a revert never recovers). [run-all.sh, tools/verify_run.py]
- **A flaky USB serial link truncates one board's capture while the radio link stays up.** The per-rep gates
  reject it (missing lines / short window); reseat the cable if it repeats. [gatt-duplex-fsu-model-20261003]
- **The 2M observer misses the frozen 95% phase-retention gate on about half of reps on this bench** — re-run
  with a fresh rep (`tools/observer-smoke.sh` retries up to 3×); never relax the gate. [q3-2m-v442-smoke-20261002]
- **Keep the host awake for unattended hardware runs.** The Mac idle-slept mid-capture (pmset log: sleep at
  12:00:16, wake 12:13:25), killing an observer capture. Run `caffeinate -dimsu` for the session.
  [onair-duplex-20261003]
- **CoC duplex FSU is measurable despite the reconnect wedge:** after flashing a central, discard its first
  connection; then per rep open the capture first and reset ONLY the peripheral (a same-boot reconnection,
  FSU lines recorded). No stalls in 51 reps across both stacks. [coc-duplex-fsu-matched-20261003]
  (Root cause found 2026-10-06, next-but-one section: the wedge is a Zephyr host stall triggered by 0 initial credits;
  with the fixed central even cold reps don't wedge, 14/14. [coc-credit-fixes-20261006])
- **The CoC reconnect wedge is not confined to ≥ 25 ms.** Resetting both boards every rep (`--cold`) stalled
  the uplink in 7 of 8 reps at 7.5 ms and 4 of 8 at 15 ms with the 2026-10 matched-arm images; the 09-06
  stall-rate study saw 0/12 at 7.5 ms. Don't rely on short intervals to dodge it: discard the first
  connection after a central boot. [duplex-fsu-followups-20261003] (Superseded 2026-10-06: trigger found and fixed,
  see "A CoC channel opened with 0 initial credits". [coc-credit-fixes-20261006])
- **A mode or a mean can hide a bimodal event mix.** CoC duplex events on air are either duplex (both sides
  send data) or one-way (one side sends only empty packets, so more exchanges fit); the per-event mode
  showed only the first kind, and the mean data-packet counts looked impossible. Look at per-event
  patterns before summarising. [duplex-fsu-followups-20261003]
- **A bursty load generator saturates events even at a low average rate.** The latency ramp sends each
  second's bulk quota as a burst, so at 25 KB/s average the events during the burst are full (5 exchanges
  at 7.5 ms without FSU, 6 with it); queue-drain effects like FSU's latency gain therefore show up far below
  the ceiling. Read per-event fill, not just the average rate. [latency-fsu-20261004]
- **Mark knife-edge predictions as parameter measurements, not tests.** At 11.25 ms the duplex model's 5th
  exchange fits only if the end-of-event reserve is ≤ 250 µs, exactly the assumed value; it missed
  (4 → 4), which pinned the parameter (250 < M < 312 µs) rather than refuting the model, because the
  pre-registration had flagged it. Check each prediction's sensitivity to fitted values before the run.
  [model-prereg-20261004]
- **An exclusion gate applied to one arm only is a bias risk; quantify it.** The duplex tools' "FSU held"
  gate runs on FSU-on reps only (to catch a revert), so an RF dip can drop an FSU-on rep while an FSU-off
  rep with the same dip stays in. Re-measured with no gate and with the gate on both arms, every duplex
  cell over two sessions moved ≤ ~1 point and kept its verdict. Re-run `held-sensitivity.py` when the
  gate rejects more reps than usual. [replication-20261004]
- **At a long interval and a weaker link, single missed events set the latency tail.** At 25 ms with the
  boards further apart, p99 sat at ~71 ms (median 46 + one interval) in both arms even at idle, masking the
  FSU p99 gain seen at close range; check the idle p99 before reading a loaded-tail delta. [replication-20261004]
- **A threshold share exaggerates a small shift when the distribution sits on the threshold.** Loaded 7.5 ms
  stop-signal RTTs cluster near 30 ms, so FSU's ~5–8 ms shift moved the `>30 ms` share 95% → 1% while p99
  moved 39.5 → 31.5 ms. Lead with percentiles; give a threshold share only with them. [latency-fsu-20261003]
- **Check "spare capacity" explanations by loading past the ceiling.** FSU's lower latency tail looked like
  headroom at 150 KB/s; at 160–190 KB/s both arms saturate, the FSU-off tail plateaus (bounded queue), and
  FSU-on still runs ~6 ms lower while carrying ~20% more bulk. [latency-fsu-20261003/latency-high]
- **A reset can glue the previous boot's last line onto the boot banner** (`interval=7*** Booting Zephyr
  …`). Parse from the line *after* the last banner, never from the banner line. [tools/latency-fsu.py]
- **Know which ACL queue depth a latency recipe uses before comparing it.** `loadramp.conf` alone = 10 TX
  buffers (the accepted 08-14 hardening result and `latency-fsu-20261003`); adding `gdeep.conf` = 64 (the
  deliberate deep-queue variant in the 08-25 control comparison and the EATT arm, both 15 ms). At 7.5 ms
  with 64 buffers the stop-signal pings queue behind up to 64 bulk packets and the tail piles up at the
  200 ms ping timeout from 50 KB/s on (censored), so FSU or pacing differences vanish. Check
  `CONFIG_BT_BUF_ACL_TX_COUNT` in the resolved `.config`. [latency-fsu-20261003/latency-gdeep-censored]
- **A send counter can't detect credit waits if the stack queues instead of failing.** In Zephyr 4.4
  `bt_l2cap_chan_send` queues an SDU when CoC credits run out rather than returning `-EAGAIN`, so the central's
  `eagain=0` never proved "no credit starvation". Log `le_chan.tx.credits` directly. Also: five single-variable
  tests (RX buffers, send pools on both sides, SDU size, CPU) left the open CoC duplex downlink pinned at ~57 KB/s
  (cause found the same day: next entry). Diagnostic builds that print untimestamped lines (thread analyzer) used to crash
  `coc-duplex-fsu.py`'s measure step; it now skips them. [coc-duplex-split-diag-20261006]
- **A deep controller TX queue delays the credit returns behind it (bufferbloat).** The open CoC duplex ~1:2 split
  is the sink's L2CAP credit PDUs waiting in its 64-deep controller FIFO (`BT_BUF_ACL_TX_COUNT`) behind its uplink
  packets: the central drains and idles ~1/3 of the time. Sink queue 64 → 20 or 8: split 1:2 → 1:1, aggregate
  173 → 184 KB/s (pre-registered, n=4 each). Host-side pools don't help, since the reordering problem is in the
  controller. The open CoC duplex FSU gains (+7.5–12%) were measured with the 64-deep queue, and their one-way-event
  mechanism comes from it. To see where a sender waits, `CONFIG_APP_CREDIT_TRACE` samples credits, the L2CAP
  queue and free controller buffers. **Amended the same day:** the sink only filled that FIFO because two credit leaks
  (next entry) had given it hundreds of surplus credits; with the leaks fixed, the recipe (64-deep on both sides)
  runs 1:1. [coc-duplex-credit-trace-20261006, coc-credit-fixes-20261006]
- **Credit counters must be reset per connection, and `seg_recv` channels keep stale `rx.credits`.** Two leaks in our
  CoC apps: `coc-duplex-central`'s `cen_avail` (and function-local `static` counters in the CoC sinks) carried over
  to the next connection, and with `seg_recv` the host never resets a reused channel's `rx.credits`, so the previous
  connection's leftovers (~60) went out as the next connection's initial credits. That is not a Zephyr defect: the
  `bt_l2cap_chan_give_credits()` docs require a reused channel to be default-initialized or memset, and our apps
  didn't (our #121544 note suggesting otherwise was corrected on the issue). Under peripheral-only resets the
  sink's balance climbed 32 → 735 and produced the published ~1:2 split. Fix: reset counters where a fresh window
  is granted and `atomic_set(&chan.rx.credits, 0)` before connect/accept. Any CoC rig that reconnects without
  rebooting both boards is exposed. [coc-credit-fixes-20261006]
- **A CoC channel opened with 0 initial credits can stall forever (Zephyr 4.4 host bug; minimal repro 6/6 vs control 0/6,
  `zephyr-l2cap-zero-credit-repro-20261007`; filed upstream as zephyr#121544).** On accept the host marks
  the channel sendable (`STATUS_OUT`) with 0 credits; the first send lowers it from the TX ready list without
  clearing `STATUS_OUT`; later credits re-raise it only if `STATUS_OUT` was clear, so data, credits and an empty
  controller sit idle (`l2cap.c` accept → `l2cap_chan_tx_give_credits(le_chan, 0)`). Racy. This is the "first CoC
  connection after central boot" reconnect wedge: our central granted its window only after connect, and the leaked
  leftovers hid it on every other connection. Fix: give the initial window before `bt_l2cap_chan_connect` (it goes in
  the request). Cold reps: most stalled before, 14/14 clean after. [coc-credit-fixes-20261006]
- **Per-segment credit returns tax every reply; they made CoC "FSU ≈ 0% at 7.5 ms".** `coc-sink` returned one credit
  per segment, and the host sends one 12-byte credit PDU per call, so the sink's replies were ~92 µs instead of
  ~44 µs empty packets and the 6th exchange per 7.5 ms event no longer fit with FSU. Same sink with batched returns:
  Zephyr +19.8%, SDC +25.2% (per-segment: −0.1% / +0.1%), i.e. CoC gains like GATT. The "CoC FSU ≈ 0% at 7.5 ms on
  both stacks" finding is retracted as a CoC property. Batch credit returns. [coc-credit-policy-20261006]
- **`pgrep -f <name>` from inside `sh -c "...<name>..."` matches its own shell.** A queued run waiting for the
  previous one to exit never started. Matching the interpreter (`pgrep -f "python3.*name"`) is NOT enough: the regex
  matches its own literal text in the waiting shell too. Wait on the PID. [coc-duplex-credit-trace-20261006]
- **Read long-interval absolute rates as ±10 KB/s day to day.** In the 2026-10-07 replication five of 32 one-way reps at
  37.5 / 50 ms read 8–25 KB/s low (all gates passed; long events are more exposed to packet loss), while 7.5–25 ms reps
  matched the previous day within ~1 KB/s. The paired FSU gains still replicated. [replication-20261007]
- **Gate runtime behaviour, not just build config.** The matched-pair check compares Kconfig only; the CoC credit bugs
  were app behaviour that every config gate passed. `tools/link_gates.py` now rejects reps on PHY, data length,
  interval changes after connect, and the CoC credit window (initial 64, running balance ≤ 64). Validated before use:
  0 new rejects on every known-good archived dataset; flags the archived leaky CoC duplex reps (balances 88–151).
  [coc-duplex-fsu-q20-20261006; validation in the 2026-10-06 commit]
- **A deep controller queue can starve a credit loop even without leaks.** With the credit bugs fixed, Zephyr CoC
  duplex at 25 ms FSU off still read 154.5 KB/s: each side's credit returns waited behind its own ~64 queued packets,
  and in ~20% of events both radio queues ran dry after 2–4 exchanges (MD = 0 on both sides). 20-deep queues: 187 → 205
  (+9.6%), = SDC. Two instrument traps hid it: the credit trace's "controller not empty" counts sent-but-unacknowledged
  buffers, and the on-air per-event *mode* (10) hid the short events; the controller's event counter (mean 8.3 vs 10.3)
  and the full histogram showed them. [coc-duplex-25ms-diag-20261006]
- **SDC budgets each exchange for a maximum-length reply (model-inferred).** One rule per controller fits all 60 one-way
  cells (Zephyr: fixed ~0.27 ms margin; SDC: start an exchange only if a max-length reply would still fit, ~0.3 ms
  margin) and predicts SDC's duplex exchange counts (12/12). It accounts for SDC's ~30 KB/s deficit at 7.5 ms one-way,
  its GATT-vs-CoC gap at 15 ms, the interval-dependent per-segment-credit cost, and duplex parity. Inferred from rates,
  not observed. [oneway-exchange-model-20261006]
- **Audit (2026-10-06) of measurement traps, all confirmed in archived data:** (a) CoC sinks' per-second
  `SINK rx: N KB/s` line is integer-truncated and its period is ~1.009–1.013 s (`k_msleep(1000)` + a blocking
  `printk`), so its median reads ~1% high; GATT's line is unbiased (period 1.000 s). FSU gains and Zephyr−SDC
  differences are unaffected (same sink both arms). `coc-duplex-fsu.py` and `oneway-fsu.py` now use the cumulative
  byte counter. (b) Integer medians make some CIs collapse ("± 0.0", "± 0.2" from identical integer values); the real
  resolution floor is ~±0.5–1%. (c) `onair-duplex.py`'s fixed 400 µs completeness gate passed half-captured FSU-on
  events (missed-packet gaps ~300–375 µs at 52 µs) but rejected the same misses at 150 µs; now a per-rep gate from
  the median gap (modes unchanged; FSU-on complete-event counts drop by 2–16 per rep). (d) `coc-duplex-fsu.py`
  reflashes only on arm changes, so with off/on/on/off the off arm gets 3 of 4 "first rep after the discard" slots
  (checked: no effect in published data). (e) Interval changes after connect, PHY and DLE are not gated by the tools
  (none seen to fire in accepted reps whose PHY/DLE lines were captured). (f) `analyze.py` joined cumulative counters across
  reconnects: on a log holding two connections it reported 31.0 KB/s for a 101.8 KB/s link; its sanity line also showed the
  FIRST PHY/DLE/interval (e.g. `DLE_tx=27` before the update to 251). Fixed 2026-10-07: rates from the last connection
  only, sanity shows final values and flags a post-connect interval change (no headline tool uses it). [coc-credit-fixes-20261006]
- **SDC's negotiated frame space depends on the PHYs in the FSU request, not on the transport.** Both builds request
  52 µs; SDC answers with its own minimum: 65 µs when the request covers 2M only (the CoC builds, `phys=0x2`) and
  70 µs when it also covers 1M (the GATT builds, `phys=0x3`). These are SDC's reported values (its FSU event); the
  observer can't measure SDC links on air. Zephyr's 52 µs is upstream Zephyr's low-latency default, confirmed on air.
  [oneway-crossstack-20261005 caps: Q3FSU-REQ/DONE vs FSU: request/updated]
- **The sink's auto-update confounds cross-stack comparisons too, not just FSU.** The August head-to-head's CoC
  "parity at 7.5 ms" (and "open +19.7% at 12.5 ms") came from sinks that renegotiated to their preferred 50 ms
  ~5 s in; a single-variable test reproduced the step (SDC 125 → 180 KB/s). Every sink in a comparison needs
  `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`, and a rate step a few seconds into a run is the signature.
  [coc-autoupdate-confound-20261005]
- **Measure a tuning caveat before asserting it.** The September "untuned SDC" caveat (written from the GATT
  central's RX 2, then copied to CoC whose SDC recipe was tuned) suggested SDC's absolute rates were understated.
  A same-session headroom test (recipe vs maximum buffers and 50 ms events on both boards) changed either stack by
  ≤ 0.7%: the caveat was wrong, and SDC's lower short-interval rates are controller behaviour. Check both boards'
  resolved configs, then run the headroom comparison. [oneway-crossstack-20261005/headroom.txt]
- **Don't run a cross-stack comparison on a lossy link.** At the wider 10-04 placement the smoke run read ~20% low
  with large per-second swings; retransmissions can affect controllers differently. Check per-second stability on
  the first reps and record the placement (`PLACEMENT.md`). [oneway-crossstack-20261005/smoke-20261005]
- **A missing log line can be a dropped log message, not a missing event.** The open central prints
  `Q2CONN` through Zephyr's deferred log buffer, which overflows at connection time in ~2% of boots
  (`--- 1 messages dropped ---`, 5 of 270 October boots, mostly at 22.5–25 ms), so the observer can't be
  tuned. Look for the drop marker before blaming the capture; the on-air tools now reconnect instead of
  rejecting (a bigger log buffer would change the firmware). [tools/onair-duplex.py, tools/observer-smoke.sh]
- **Update SHA256SUMS in the same commit that rebuilds an archived image, and hash after any path rewrite.**
  The 09-07 rebuild of `v442-coc-sink.hex` left its checksum stale, and the 09-05 history rewrite (home dir →
  literal `$HOME`) changed text files after hashing. `python3 tools/scrub-paths.py verify <SHA256SUMS>` now
  accepts raw, reversibly scrubbed and legacy-`$HOME` bytes; 15 known mismatches remain (EVIDENCE-INDEX).
- **Publication hygiene: a raw-byte grep misses strings inside compressed files.** Tracked clangd `.idx`
  caches held local paths inside zlib streams; decompress-scan binaries before publishing.

## A. Measurement / design traps (how runs silently hand you wrong data)
- **Measure the treatment over a window where it is IN EFFECT — not the whole run.** FSU negotiates
  *mid-capture* (open ~6 s, SDC ~16 s of a 30 s run); a whole-run median **blends pre- and post-FSU**
  operating points and understates the on-arm. This distorted the SDC-GATT curve (a 50 ms cell read 172
  blended vs 184.5 post-FSU → a fake "steep decline to +7%"). Parse the `spacing=` onset timestamp and take
  throughput strictly after it; reject a window that straddles the transition. [gatt-fsu-clean-20260908 §2]
- **Quote the SUSTAINED window, not the good first second — a degrading metric hides in an average.** The
  §11.3 dedicated-lane control RTT was published as "~33 ms (min 19 / max 69)" from a single run — but that
  was its *first* saturation window; the channel then settles worse. Re-measured over the sustained window
  (t≥10 s, n=8): **32.2 ms ± 0.3, 100% over the 30 ms budget, worst 87 ms** — reliable, but the "~33 ms" had
  implied an under-budget pass it never sustained. For any latency/throughput-under-load number, measure the
  steady window a deployment actually lives in, not the transient. [coc-dedicated-lane-retest-20260909]
- **Re-run n=1 / single-session claims before they become load-bearing.** Two safety numbers shipped at
  n=1–2 (dedicated-lane RTT, 50-reset recovery). Re-running raised confidence *and* corrected one: reset
  recovery held (100/100, mean 4.54 s), but the dedicated lane got more honest (see above). A claim an
  architecture is built on should not rest on one run. [coc-dedicated-lane-retest / reset-recovery-100-20260909]
- **n=1 is not a rate; a short passing window / one survivor ≠ stability.** Intermittent effects need many reps + a *rate*. (`n=141`/12 s "passed" but failed a soak; a 117 s "stable" run was later 1-of-6.) [coc-duplex-artifact]
- **Real stall vs dead link vs balanced.** Real stall = one direction healthy + the other 0. Both 0 = dead (exclude). **A *balanced* duplex rep has downlink ~75, NOT ~150** (it drops to share airtime) — do **not** gate validity on `downlink>100`, that excludes balanced runs. [duplex-independent-abba / stall-rate 20260906]
- **Which reset you do matters.** Reset the *central* → next CoC is "first-since-boot" (triggers the reconnect wedge); reset *only the peripheral* → central stays booted (subsequent = steady-state). Don't conflate. [uplink-coc-diag deconfound-v3] (2026-10-06: the "first-since-boot" wedge was
  the Zephyr 0-initial-credit stall triggered by our central; fixed in the apps [coc-credit-fixes-20261006].)
- **Duplicate-capture trap.** Verify each rep is genuinely independent (distinct session/AA + byte content); byte-identical "reruns" once passed as independent n. [deconfound-v2]
- **Un-pinned operating point = fake effects** (the retracted "RF-collapse"). Log interval/SDU/spacing/PHY/DLE/distance; compare like-for-like. [tx-staging-20260826]
- **"2M enabled in the config" ≠ operated at 2M.** Auto-2M engages only after the post-connect `PHY tx=2 rx=2` callback; confirm the *active* PHY from a logged line per run, never from Kconfig.
- **App-echo double-counts the air → a false "hard ceiling."** A peripheral that echoes every write flies each byte twice; the rate looks half. Use *sink* mode for throughput, not echo.
- **Reset-assisted "soak" is not a soak; a halted board is silent** (halt-on-fault, no auto-reboot) — "silent" can be dead, not passing. Only *uninterrupted* runs measure stability.
- **Echo-rig "symmetry" is an artifact** (reverse = forward echoed → coupled); measure symmetry only on an *independent* bidirectional rig. [duplex-independent-abba-20260906]
- **Reproduce deltas, not absolutes** (KB/s are RF-day specific); **sender-accepted ≠ delivered** (score sink-delivered counters); KB/s = /1024.
- **On-air NEGATIVES need a positive control.** A lossy sniffer's "zero 52 µs gaps" can't tell "none on air" from "I missed them"; HCI-completion without a captured RSP = *incomplete capture*, not a physical null. Prove the sniffer reads a *known* sub-150 spacing first. [fsu-m0-onair PROVENANCE]
- **On-air capture hygiene:** non-monotonic pcap = two sessions concatenated (reject); pick the AA from the target `CONNECT_IND`, not by packet frequency; "drops=0" must mean the *complete* set (ring-full=0 AND addr==end AND recorded==frozen AND no reversal), else it's a double-boot artifact = INVALID. [fsu-m0-onair; observer-q1 DISPOSITION]
- **Use `tools/analyze.py` / `tools/capture-tool.py` — don't write new parsers/capture tools.** [AGENTS.md §9]
- **A silently-ignored `--calib` = wrong baseline, not an error — two traps that both use the *idealized* reference and still print "OK".** (1) A **non-None default arg shadows the artifact**: `q3_2m_analyze` defaulted `calib_frozen=2783`, so `q3_analyze` saw non-None and never loaded the passed `--calib` → gated the rig's 2791 t baseline against the ideal 2783. Default such params to `None`; apply the built-in only when no artifact is given. (2) A **flags-before-positionals CLI drops a *trailing* flag**: the analyzer only parsed `--calib` before the positionals, but the runner appends it *after* → silent no-op. Parse flags in any position. [q3-2m-corroboration-20260906]
- **2M symmetric calibration controls need TX balance, not just distance.** The ≤6 dB symmetric-RSSI gate failed at ~35 dB even with equidistant boards because the endpoint firmware TX was asymmetric (central `PLUS_8`, periph `MINUS_20` = 28 dB baked in). Balance received power (equalize TX minus each endpoint's path advantage — here central +1 / periph +8) → sep 4 dB, controls ACCEPT, `combine_calib_2m` → ESTABLISHED 2791 t. [q3-2m-corroboration-20260906]
- **The FSU-engaged confirmation prints ONCE at connection setup — a mid-stream capture never sees it.** `Q3FSU-DONE … spacing=52` (CoC) / `FSU: updated … spacing=52 us` (GATT) fires ~1-6 s after reset, then never again. `run-all.sh` and the A/B harness open serial mid-stream, so they capture throughput but CANNOT distinguish `spacing=52` from a silently-clamped `spacing=150`. To *verify FSU engaged*, always capture from BOOT (open serial, then reset into it). This is precisely how the broken CoC sink went unnoticed — the KB/s looked plausible. [fsu-config-audit-20260907]
- **Map a result to its source app by the PROVENANCE CHAIN (hex → prebuilt README/SHA256SUMS → REPRODUCE recipe → app+overlays), NEVER by the app's name.** The GATT throughput/FSU result is produced by `z54-lat-central` (a *latency*-named app), not by the apps literally named `z54-gatt-*` (which are earlier pre-FSU builds). A config audit keyed on the name "gatt" looked at the wrong apps and wrongly concluded "GATT FSU has no config." Prevention: every app carries a README stating which documented result it backs, and there is one canonical result→app map. "Not referenced by the current FSU recipes" ≠ "useless" — several unreferenced apps back the documented *pre-FSU* bandwidth/latency benchmarks. [fsu-config-audit-20260907]
- **REPRODUCE recipes were not all build-tested — a recipe can pass a `-D` for a symbol the app doesn't define, which HARD-ABORTS the build** (`attempt to assign … undefined symbol … Aborting due to Kconfig warnings`). The duplex central recipe passed `-DCONFIG_APP_SDU_SIZE=480` (that app has no such symbol; SDU is fixed via `BT_L2CAP_TX_MTU`); the duplex sink recipe omitted `open-fsu.conf` → silent-150. Before publishing, dry-build every recipe and confirm the intended feature engaged. [fsu-config-audit-20260907]

## B. Retracted / superseded — do NOT re-assert
- **DLE=251 as a *deterministic* stall trigger** → RETRACTED; intermittent. [tx-staging-20260826]
- **Zephyr #46073** → wrong (IPSP); correct lineage **#76737**. [UPLINK-RECONNECT-FINDINGS]
- **~3.5× uplink asymmetry is structural** → RETRACTED; firmware bug (peripheral never called `bt_conn_le_data_len_update`); fixed → uplink ≈ downlink. [UPLINK-RECONNECT-FINDINGS]
- **"CoC is more RF-fragile than GATT"** → DISPROVEN (matched-payload equal; 480 B drop = segmentation). [coc-vs-gatt-rangetest-20260825]
- **FSU duplex +14% / ~200 KB/s (SDC)** → did NOT reproduce head-to-head; withdrawn. [sdc-vs-open-headtohead-20260814]
- **"Duplex FSU is a measured null / duplex airtime is saturated"** → WRONG as a general claim. Resolved
  2026-10-03 with matched, held-FSU GATT echo duplex: **no gain at 7.5 ms (+0.3% ± 1.0%) but +9.5% ± 2.1% at
  25 ms** [gatt-duplex-fsu-matched-20261003]. The 08-24 run's FSU-off arm was confounded (no FSU code), yet
  its 25 ms gain held up. History of the quarantine (before 2026-10-03; superseded by
  coc-duplex-fsu-matched-20261003 and replication-20261004): The 09-06 null (test 3)
  used `coc-duplex-sink` with auto-update ON and PREF 50 ms; FSU-on logs carry only the latched `fsu=52`
  token, never `spacing=52`, and no interval line — exactly the lesson-#1 revert signature. The held-verified
  rerun covers only SDC CoC 7.5 ms (+0.5%, n=3); the only positive data (08-24 GATT echo, +6–12% at ≥25 ms)
  has an unverified peripheral floor. (Was: "duplex FSU is OPEN"; "saturated" was inferred, never measured.)
  [duplex-campaign-20260906 test 3; coc-duplex-fsu-interval-20260908; duplex-fsu-abba-20260824]
- **Duplex "symmetry ≤0.1%"** → echo-rig artifact; independent duplex intermittently uplink-stalls. [duplex-independent-abba-20260906]
- **"CoC-duplex ~168 ≈ one-way" and "~103 inefficient"** → both ARTIFACTS (central under-received; app under-buffering, not a controller limit). [systematic-sweep-20260814]
- **"Refill wall below ~35-41"** → RESOLVED = tIFS airtime; no refill lever. [tx-staging §11.1]
- **"+5% from silicon" (nRF52→nRF54)** → within ±10-20% session RF scatter, not a demonstrated gain.
- **Both fsu-m0 on-air "negatives" (open 2M/52 & SDC/70)** → RETRACTED as capture-incomplete, NOT physical nulls; the "nRF sniffer is too lossy/dead-end" claim also retracted. (On-air FSU was later **formally ACCEPTED** at 1M and 2M — see §C; observer-based, not professional-analyzer qualified.) Indirect +5.72% stands. [fsu-m0-onair PROVENANCE]
- **EATT "worsens 30 ms/timeout at every load"** → RETRACTED (over-strong); EATT is a tail reshaper, not a 30 ms fix. First EATT run (08-26) INVALID (seq-correlation defect) — don't cite. [eatt-latency-20260826]
- **"underflow -1956 / cyclic reconnects / abrupt-teardown trigger / Order-A / PHY-collision cause"** → all RETRACTED (instrumentation bug / mis-count / confounds). [uplink-coc-diag deconfound v1-v3]
- **CoC one-way FSU is ≈+20% (HELD) — the earlier "flat/~0%/below-ceiling/RF" conclusion was wrong twice over — an auto-update-revert artifact.** History (a cautionary tale): +14.4% (Aug) → mis-"retracted" as a below-ceiling RF-session artifact → **that retraction was itself the error.** Root cause: the invalidating runs (`coc-fsu-interval`, the CoC cells of `fsu-campaign`, and the archived-binary "replays") all used sinks with `BT_GAP_AUTO_UPDATE_CONN_PARAMS=y`, so the ~5 s param-update **silently reverted tIFS to 150 µs mid-run** while the latched `fsu=52` token kept printing — the harness measured post-revert (150 µs) throughput → false "flat." Re-measured with **auto-update-OFF sinks, held-verified end-to-end** (`coc-fsu-corrected-20260908`, n=15): **CoC/open +20.4% ± 1.0%, GATT/open +19.7% ± 0.5%** (both ~186 on / ~155 off, held 100%); SDC/CoC +11.6%, SDC/GATT +25.1% (`sdc-fsu-corrected-20260908`). **CoC and GATT FSU are BOTH ≈+20% — NOT transport-dependent.** The August +14.4% was FSU-*held* and essentially valid. See lesson #1 (verify HELD throughout) + `tools/verify_run.py`. [coc-fsu-corrected-20260908; sdc-fsu-corrected-20260908 — SUPERSEDES coc-fsu-rootcause/interval/fsu-campaign-CoC]

## C. Domain gotchas
- **Two distinct duplex stalls:** (1) steady-state head-of-line starvation (downlink saturates the conn TX queue → uplink credit-return stalls); (2) first-CoC-since-central-boot reconnect wedge. Terminology tension (unresolved, don't assert): coc-duplex-artifact calls (1) a *"firmware/host bug, NOT a credit tax"*; later notes say "credit/head-of-line tax."
  (2026-10-06: (2) is the Zephyr 0-initial-credit stall [coc-credit-fixes-20261006]; with the fixed apps, (1) remains
  only as credit-loop starvation from a 64-deep controller queue, fixed by 20-deep queues [coc-duplex-25ms-diag-20261006].)
- **The 25 ms uplink stall is the first-CoC-since-central-boot RECONNECT WEDGE, not steady-state starvation:** cold-reset (reset both) stalls 10/12 at 25 ms, but periph-only reset (central stays booted) stalls **0/12**. 7.5 ms balanced 12/12 either way with the 09-06 images (~77+77); with the 10-03 matched-arm images a cold first connection also wedged 7/8 at 7.5 ms and 4/8 at 15 ms (see A1). Balanced ≈ shared-airtime ceiling (~155), NOT 90+90; a *subsequent*-connection 25 ms link can run **uplink-dominant** (~110/55, the freshly-rebooted side's TX wins). [stall-rate-20260906]
  (2026-10-06: root cause = the Zephyr 0-initial-credit stall; fixed, 14/14 cold reps clean [coc-credit-fixes-20261006].)
- **Duplex FSU gain is quantized by whole exchanges per connection event, so it is not monotonic in
  interval.** One echo exchange = 2 full PDUs + 2 gaps (2396 µs at 150 µs, 2200 at 52, 2236 at 70); FSU adds
  throughput only when the saved time fits one more exchange (measured 3→3 at 7.5 ms, 6→6 at 15 ms, 10→11 at
  25 ms → 0%, 0%, ~+10%), confirmed on-chip and on air by the nRF52 observer. **CoC duplex follows the same
  pattern on both stacks** once the test apps' credit bugs are fixed and Zephyr uses 20-deep controller queues
  (Zephyr +0.2 / +2.5 / +9.6%, SDC +0.0 / +2.8 / +10.2%, replicated) [coc-duplex-fsu-q20-20261006, replication-20261007].
  (Superseded: the earlier open CoC gain at every interval, +7.5–12%, came from an uneven split produced by credit
  leaks, which created one-way events that gain ~20% [duplex-fsu-followups-20261003 §3, coc-credit-fixes-20261006].)
  Predict the per-event exchange count before choosing intervals for an FSU claim.
  [gatt-duplex-fsu-model-20261003, onair-duplex-20261003, coc-duplex-fsu-matched-20261003]
- **The open controller ends events with a packet that is cut off** (observed on air: a trailing packet that
  fails CRC in every event except 7.5 ms FSU-off; never acknowledged, resent next event). Cause inferred
  (TX started past what fits before the event deadline). Costs airtime/energy, not throughput.
  [onair-duplex-20261003]
- **Throughput ceiling = tIFS airtime** (~10 PDU/event @15 ms); levers = FSU / PHY / packing, not buffer depth.
- **On-air FSU tIFS: FORMALLY ACCEPTED at both 1M and 2M** (nRF52 observer, physically measured). **1M 150→100 µs** — `fsu-q3a-20260812`. **2M 150→52 µs** — `q3-2m-accept-20260906`: 3 primary mid-step AGREEMENT cells (`|d|≤1`, on-air==on-chip xval `|d|≤0.06`) + 4-cell ABBA-CONFIRMED (drift-cancelled step 1567.75 t, `|d|=0.25`), all non-smoke / clean-tree / provenance-bound through the ported 2M machinery (`assert_fsu_config_2m`, `analyze_q3_2m`, `combine_calib_2m` → ESTABLISHED calib 2791 t, `combine_abba_2m`). **Remaining honesty:** accepted *by our protocol via the loss-limited nRF52 observer*, NOT independently qualified by a pro analyzer (Ellisys/Frontline) — that equipment gap is unchanged. The *earlier* 2026-08-07 2M negatives were capture-incomplete — **superseded**. Observer is **nRF52-only** (bare-metal PPI/TIMER0/`END`; nRF54 = DPPIC/PHYEND — no port).
- **2M ACCEPT needs TWO peripheral builds** (both assert-pass; the asserter deliberately doesn't gate auto-update): **mid-step** uses auto-update-OFF periph (pinned interval → clean baseline plateau; the auto-update variant corrupts the mid-step baseline via a mid-window interval change → REJECT), while **steady/ABBA** uses auto-update-ON periph (pref 50 ms) so the runner event-gates FSU on the ~5 s conn-param update and the negotiated frame space persists. [q3-2m-accept-20260906]
- **The Q3 analyzer/gates are 1M-frozen — a real 2M capture is falsely rejected until they're ported.** Four gates hardcode 1M and must honor an `EXPECT_PHY`-style override for 2M: config-binding `observer PHY==1M`, request-integrity `registered min/max` + `phys` (register **equals** the firmware request, e.g. `52→150 phys=0x2` for `fsu-f52`, mirroring the 1M f100 `100→150`), and the on-chip cross-val `phy!=1` (**two** call sites). The `--calib` loader (`validate_calib`) rejects a naked json by design (anti-forgery) — a legitimate 2M calib requires FSU-off control cells + `combine_calib`. [q3-2m-corroboration-20260906]
- **2M observer capture: retained empties overflow the 2048 ring — fit it by raising the conn interval, not by dropping empties.** A 2M empty PDU (~320 t) needs `AIRTIME_MIN_TICKS=256` (the 1M 500 t floor drops it → breaks the per-tIFS gap chain), but retaining empties ~doubles records past the RAM-bound 2048 ring (`ring_full_drops`≠0 → structural REJECT). RING_N can't grow (record=20 B → 2048 already = 40 KB of 64 KB). Fix = raise the connection interval (7.5→20 ms) so 30 s fits; tIFS is interval-independent. The observer CMakeLists did **not** forward `-DAIRTIME_MIN_TICKS` (silent no-op) — forward it. [q3-2m-corroboration-20260906]
- **95% phase-retention is a frozen instrument gate: missing it = redesign the observer, NOT retune the threshold.** The loss-limited single-antenna nRF52 observer clears 95% only on good reps at 2M; do not lower `RETENTION_MIN`/`Q3_PHASE_RETENTION_MIN` to force a pass (the gate's own note says redesign). Diagnostic relaxation to *read* the step is fine **if restored** and disclosed; it must never ship in a production result. [q3-2m-corroboration-20260906]
- **FSU request order is strict & fragile:** connect → 2M → full DLE → (defer past any GAP auto-param-update) → FSU. Requesting early → `-EACCES`; a param-update landing *after* FSU **silently reverts tIFS to 150 µs**. [coc-latency-under-load-20260825; fsu-m0-onair]
- **Missing `BT_AUTO_DATA_LEN_UPDATE` → 27-byte PDUs, ~37 KB/s** — the recurring missing-DLE failure (3rd sighting). SDC: min SCI interval 750 µs; HCI CE-length floor = 1 (0 → `-EINVAL`). [sdc-bench-20260806]
- **Bench Kconfig features default `n`** (`APP_HB_SENDER`, `APP_SAFETY_WATCHDOG`) — "builds clean" can mean *neither was compiled*; enable via `-DEXTRA_CONF_FILE=bench.conf` and grep the built `.config`. Enabling the HB sender shifts ping RTT (~11.9→~8.9 ms) → don't mix bench & latency-baseline builds. [safety/STOP-MEASUREMENT.md §2]
- **Safety bench:** a STOP pin floats HIGH (hi-Z) through reset/reboot (~400 ms) → a discrete external pull-down is **mandatory and must be measured** (crash-vs-power-loss is indistinguishable without it); an external WDT watching a firmware-generated HB line is **fail-*silent* only**; write "accepted for queueing" ≠ transmitted, and Zephyr may drop the completion cb on teardown (generation-tag the gate, free on disconnect, serialize compare-and-clear). [safety/STOP-MEASUREMENT.md]
- **FSU silently doesn't engage if the RESPONDER lacks `CONN_INTERVAL_LOW_LATENCY` — verify `spacing=52`, not just the request.** The 52 µs `EVENT_IFS_LOW_LAT_US` floor is clamped to 150 unless `CONFIG_BT_CTLR_CONN_INTERVAL_LOW_LATENCY=y` on **both** ends; the committed `coc-sink/open-fsu.conf` was missing it (central had it), so FSU negotiated `spacing=150` and the on/off delta read ~0% (looks like "FSU does nothing"). Always confirm the achieved `spacing=52`/`fsu=52` in the log before trusting an FSU throughput delta. **And reuse the proven archived config / REPRODUCE recipe verbatim — do not reconstruct it** (reconstructing `open-fsu.conf` re-introduced this; the +14.4% run used `/tmp/coc-cen-app` + `sink-fsu.conf` which had the floor). [coc-open-fsu-20260813; fixed in coc-sink/open-fsu.conf 2026-09-07]
