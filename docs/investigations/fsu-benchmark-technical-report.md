> **Partly superseded (2026-10-06):** the CoC one-way numbers below used a sink that returned one L2CAP credit per
> segment. The "CoC ≈0% at 7.5 ms" threshold is an artifact of that sink (with batched returns: +20% Zephyr, +25% SDC),
> and it also depressed SDC's 15 ms CoC rate (141 → 156 KB/s without FSU). Current CoC one-way results:
> `debug-evidence/oneway-coc-batched-20261006/` (replicated in `replication-20261007/`). GATT results here are unaffected.

# FSU throughput benchmark — technical report (results & understanding)

**Date:** 2026-09-08 (revised after critical review). **Rig:** 2× nRF54L15-DK (BLE 6), 10 cm, clean
bench, **2M PHY (log-confirmed all 160 runs) + DLE-251 (configured; negotiated DLE not logged — §7)**. **Stacks:** open Zephyr `ll_sw_split` **v4.4.2** + our `fsu-m0` FSU patches;
Nordic **SoftDevice Controller** via **NCS v3.4.0** (Zephyr host 4.4.0). Supersedes the retracted
`coc-fsu-root-cause-statement.md`.

> ### Status & confidence (read first)
> This is a **provisional, clean-bench** characterization. It **combines several campaigns, each one
> session** (not one unified session), and the duplex result rests on only **three accepted pairs**. After critical review, the
> claims are scoped as follows:
> - **Solid: the CoC FSU interval threshold** (clean on/off isolation, window-corrected, n=7–8): FSU is
>   flat at 7.5 ms and +16–21% at ≥15 ms, on both stacks.
> - **Clean (re-run): the GATT deltas.** The original GATT off-arm dropped the whole FSU feature package
>   (a confound caught in review — 36 differing config symbols). GATT was **re-run 2026-09-08 with a
>   matched off-arm** (identical feature package, requests 150 µs) + FSU-capable sinks, verified by the
>   `check_matched_pair.py` design gate (10/10 pairs MATCHED) and 160/160 reps accepted. §3's GATT rows
>   are that confound-free re-run [`gatt-fsu-clean-20260908`].
> - **Window-corrected: SDC.** SDC GATT negotiates FSU ~16 s (SDC CoC ~2.4 s) into a 30 s capture; throughput is recomputed
>   over the **post-FSU window** (earlier whole-run medians blended two operating points and understated
>   SDC on-throughput). The SDC GATT "steep decline" was largely that artifact.
> - **Not a deployment recommendation** — see §8. **All CIs are Student-t, within-session only** (§7).

## 1. Findings (scoped)
- **CoC one-way FSU has a real interval threshold** (clean isolation): **≈0% at 7.5 ms, +16–21% at
  ≥15 ms**, on open *and* SDC.
- **GATT one-way FSU is large and real** (clean matched-arm isolation, confound-free re-run, n=8):
  **+17–20% across all intervals on open**, and on SDC **+25% at 7.5 ms, falling by 25 ms to ~12–13%** (flat
  thereafter).
- **Open vs SDC:** *not* cleanly paired here (separate campaigns; see §4) — no parity headline is claimed
  from this report.
- **Duplex:** wedge-limited; a clean FSU number exists only for **SDC at 7.5 ms** (≈0%, n=3). Broader
  duplex-FSU claims are not supported (§5).

## 2. Method — and its limits (honest)
The earlier "CoC FSU ≈0%" was wrong: the responder's GAP auto-param-update reverted tIFS to 150 µs
mid-run while the `fsu=52` token stayed latched. Fixes applied here:
1. **Auto-update-OFF responders** (`BT_GAP_AUTO_UPDATE_CONN_PARAMS=n`) so FSU cannot revert mid-run.
2. **`tools/verify_run.py` gate** — what it *actually* does (not more): it (a) checks the throughput
   time-series does not **step down** mid-run (an *inference* of "held", since the token is latched),
   (b) records PHY/DLE/interval/version and the spacing token, (c) rejects a dead link or (for open) a
   missing `spacing=52`. **Caveats:** DLE/interval/version are recorded but mismatches are *warnings*,
   not rejections; missing operating-point lines pass; the SDC spacing token (65/70 µs) is not asserted;
   and it uses its own throughput parser rather than the canonical `tools/analyze.py` (a known duplication
   to reconcile). It does **not** independently prove tIFS held — only that throughput didn't collapse.
3. **Post-FSU windowing** — throughput is measured after FSU negotiates, never straddling it. The clean
   GATT harness parses the cen-log `spacing=` onset and **clips** the late window to start after it; the
   derived CoC re-analysis [`postfsu-reanalysis-20260908`] uses the **last 12 s** (post-onset by construction,
   since CoC negotiates FSU by ~2.4 s on SDC / ~6 s on open). **Counterbalanced, n=7–8; Student-t 95% CIs.**
4. **Matched-pair design gate** (`tools/check_matched_pair.py`, GATT re-run) — the on/off builds' *resolved*
   `.config` must differ ONLY in the requested frame space; any other differing symbol **refuses the run**.
   This is what catches a confounded A/B that per-capture gating (2) is structurally blind to. All 10 GATT
   pairs MATCHED; sinks verified FSU-capable + auto-update-OFF.

## 3. Results — one-way FSU vs interval (post-FSU window, n=7–8, Student-t 95% CI)
Absolute KB/s are RF-day-specific; the deltas and shape are the transferable results.

**Open (Zephyr v4.4.2):**
| interval | CoC off→on | CoC FSU | GATT off→on | GATT FSU |
|---|---|---|---|---|
| 7.5 ms | 154→156 | **+1.0% ± 1.5%** | 158→189 | **+19.8% ± 0.2%** |
| 15 ms | 155→187 | +20.7% ± 1.8% | 157→189 | +20.2% ± 0.3% |
| 25 ms | 157→186 | +18.1% ± 0.7% | 161→188 | +16.9% ± 0.7% |
| 37.5 ms | 158→186 | +18.2% ± 2.6% | 163→193 | +18.7% ± 0.7% |
| 50 ms | 159→184 | +16.1% ± 2.8% | 165→192 | +16.6% ± 1.0% |

**SDC (NCS v3.4.0):**
| interval | CoC off→on | CoC FSU | GATT off→on | GATT FSU |
|---|---|---|---|---|
| 7.5 ms | 124→125 | **+0.7% ± 0.8%** | 126→157 | **+24.6% ± 0.6%** |
| 15 ms | 141→156 | +11.3% ± 0.9% | 142→173 | +21.9% ± 0.2% |
| 25 ms | 149→178 | +19.6% ± 0.7% | 160→179 | +12.2% ± 0.7% |
| 37.5 ms | 156→181 | +16.6% ± 0.6% | 163→182 | +11.7% ± 1.2% |
| 50 ms | 156→183 | +17.1% ± 2.4% | 161→183 | +13.2% ± 1.4% |

*(GATT rows = the confound-free re-run [`gatt-fsu-clean-20260908`]: matched off-arm + FSU-capable sinks,
post-FSU window, Student-t, 160/160 accepted. They **supersede** the earlier confounded/window-mixed GATT
numbers. The re-run changed three things at once — matched off-arm, sink capability, analysis window — so the
campaign-to-campaign difference is **not** decomposable to any single cause; the earlier GATT deltas are
simply not trustworthy, not "off by N points." Separately, re-analyzing the SAME original SDC logs with only
the window changed (single variable) showed the old whole-run "+7% at 50 ms" was a window artifact (→ ~+14%)
— persisted in [`postfsu-reanalysis-20260908`]. CoC rows are the original sweeps [`fsu-interval-sweep` /
`sdc-fsu-interval-sweep`], on/off isolation already clean, re-windowed post-FSU; each a single session,
different campaign from the GATT re-run.)*

## 4. Open vs SDC — no parity claim
The corrected 2×2 came from **separate campaigns**, not a counterbalanced same-session comparison, and
the off-arm absolutes differ materially (open/SDC CoC 154/141, GATT 158/126 KB/s — clean §3 values). **This report does
not claim open≈SDC parity.** The ledger-backed same-bench comparison is
[`sdc-vs-open-headtohead-20260814`] (CONFIRMED by ABBA×2 non-overlap): near parity in the tested
GATT/50 ms and CoC/7.5 ms cells, but open **+19.7%** at CoC/12.5 ms — i.e. not blanket parity. Cite
*that* for open-vs-SDC claims; do not infer parity (or its absence) from the tables here.

## 5. Duplex — wedge-limited, one condition
Duplex is dominated by the reconnect/uplink-stall wedge under cold reconnect. Only **SDC at 7.5 ms**
produced enough healthy paired rounds (n=3) to compute a delta: **FSU ≈ +0.5% ± 1.7% (Student-t, n=3 —
wide; persisted in [`postfsu-reanalysis-20260908`])**. All longer-interval SDC cells and **all** open-duplex cells produced **no accepted pairs**
(stall). The defensible statement is limited to **SDC-duplex, 7.5 ms**. The stall-rate itself is the
useful result: it climbs from ~37% (7.5 ms) to 100% (≥37.5 ms) under cold reconnect — a reconnect
*reliability* finding, worst-case; steady-state (periph-only reset) was not measured. **The "duplex is
airtime-saturated so FSU has no headroom" explanation is a hypothesis, not demonstrated** — if the event
were airtime-limited, FSU should help; the flat result more likely reflects an event-packing or host
bottleneck we have not isolated.

## 6. Understanding — the working hypothesis (not proven)
FSU shortens the idle inter-frame gap; the working model is that it converts to goodput only when the
event is airtime-limited *with data queued to fill the freed time*. This fits the **CoC threshold**
(refill-limited at 7.5 ms → ≈0%; airtime-limited at ≥15 ms → +16–21%). GATT is now also a clean test and
fits it (airtime-limited at every interval → +17–20% throughout on open; the SDC decline is consistent
with SDC approaching its own ceiling at longer intervals). Duplex remains **not** a clean test (saturation
unproven — §5). On-air, the tIFS reduction is physically confirmed on the **open** controller
(nRF52 observer, 1M→100 µs & 2M→52 µs formally accepted, separate work); the **SDC** controller
negotiates a *different* spacing (65/70 µs) and has no on-air confirmation here.

## 7. Uncertainty budget
- **Tier 1 — within-session statistical:** Student-t 95% CIs. The GATT re-run is **160/160, n=8** every
  cell (±0.2–1.4%); one CoC-SDC cell (50 ms) from the original sweep is n=7 (the other original reject was
  the now-superseded GATT/SDC 37.5 ms cell). Overall ±0.2–4.0%.
- **Tier 2 — systematic:** the FSU-revert error and the GATT feature-package confound are now **gated
  out** (`verify_run` held-check; `check_matched_pair.py` design gate). The residual systematic limit is
  the verifier's remaining *soft* checks (§2: PHY/DLE/interval/version recorded as warnings; own parser).
- **Tier 3 — between-session / RF-day (NOT in the CIs) — top residual:** each **one-way** campaign is **n=8
  reps within its own single session** (the GATT re-run, the two CoC sweeps, and duplex are *separate* 2026-09-08
  sessions; the open-vs-SDC head-to-head ref is 2026-08-14). No result is replicated across days, and cross-campaign absolutes
  are not counterbalanced. Duplex additionally rests on only **3 accepted pairs**.
- **Tier 4 — deployment regime (UNMEASURED):** clean bench, 10 cm; degraded/ranged RF, mobility,
  interference, and long-duration behaviour are untested — the FSU delta may shrink there.
- **Absolutes carry ±10–20% RF-day variance and do not transfer; deltas + shapes do.**

## 8. Operating-point observation — NOT a deployment recommendation
For a teleoperation safety link this campaign supports only a **provisional laboratory candidate**, not
deployment guidance. Observed **on open**: GATT + FSU throughput is essentially flat across intervals
(189–193 KB/s), so 7.5 ms carries **no throughput penalty** for its low-latency floor (whereas CoC FSU is
≈0% at 7.5 ms). **On SDC the tradeoff is real:** GATT throughput climbs 157→183 KB/s from 7.5→50 ms — about
**14% less throughput at 7.5 ms** — so on SDC the low-latency interval costs throughput; pick the operating
point per stack. Caveats that must clear before this
is deployment guidance: one-session-per-campaign (§7 T3) and the entirely-unmeasured deployment regime (§7 T4). **Note:** 7.5 ms is the **legacy** minimum connection interval and the
minimum tested here; Bluetooth Core **6.2 "Shorter Connection Intervals"** permits down to **375 µs**
(untested here). FSU on the open stack needs our `fsu-m0` controller (not in released Zephyr); SDC ships
FSU. Safety-relevant metrics (latency-under-load tail, reliability/soak, end-to-end STOP budget, range,
power, coexistence) are **out of scope of this throughput report** and must be assessed separately.

## 9. Open items (what it takes to firm this up)
1. **Clean GATT off-arm — DONE** (2026-09-08, [`gatt-fsu-clean-20260908`]): re-run with a matched off-arm
   (full FSU package, requests 150 µs) + FSU-capable sinks; §3 GATT rows updated. Enforced going forward by
   the `check_matched_pair.py` design gate (refuses any confounded A/B before it runs).
2. **Multi-day replication** (Tier 3) — the highest-value follow-up; each campaign is only one session.
3. **Degraded-RF / range / mobility sweep** (Tier 4) — the deployment regime.
4. **Reconcile the verifier** with `analyze.py` (single parser), make PHY/DLE/interval/version *rejections*
   not warnings, assert SDC spacing<150, and add an `analyze.py` regression test.
5. **Archive resolved `.config` + exact build invocations** per cell (see §10) so builds are reconstructable.
6. **Duplex steady-state** (periph-only reset) — the useful duplex measurement (FSU there is a known ~0%).

## 10. Reproducibility status (honest)
The GATT re-run [`gatt-fsu-clean-20260908`] is the **target state**: it archives all 22 resolved `.config`
files (`configs/`), a firmware SHA256 manifest, the build recipe + design gate (`build_gattclean.sh`), and
`METHODOLOGY.md` — reconstructable. The **older** dirs (the CoC rows, duplex, etc.) contain raw logs +
firmware SHA256 + gated harnesses but **not** resolved `.config`, and their harnesses reference /tmp build
dirs + hardcoded serials — so those exact builds can't be reconstructed after /tmp is cleared. Extending the
re-run's config-archiving to the remaining dirs is open item §9.5. **Controller provenance (the 22 hexes
split by stack):** the **11 Open builds** are Zephyr controller SHA
`fee9fbc620959b218cda34602d7653d21f7f6a51` (`v4.4.2-16-gfee9fbc6209`, `github.com/teleop-bench/zephyr`) —
the exact SHA, *not* the mutable `fsu-m0-v442` branch; the **11 SDC builds** use the Nordic SoftDevice
Controller from **NCS v3.4.0** (`nrfutil toolchain-manager … --ncs-version v3.4.0`, Zephyr host 4.4.0).
Application/overlay sources are in the repo commit adding this evidence dir. Gate/regression: `tools/check_matched_pair.py`
(+ `test_check_matched_pair.py`), `tools/verify_run.py` (+ `test_verify_run.py`).
