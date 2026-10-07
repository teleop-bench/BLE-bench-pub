# FSU 2M/50 ms saturation preflight — two rig-config artifacts fixed; corrected run = config-gate PASS, saturation-gate FAIL (~128 KB/s, ~77% of model; residual cause OPEN)

**Progression (all 2M / 50 ms, open `fsu-m0`, FSU off):** two early runs were invalidated by
**rig configuration**, not by any controller/event-fill limit — (1) the peripheral **echo** was
left on (bidirectional ping-pong), and (2) **MTU 23** capped writes to 20 bytes. The corrected
**sink-only + MTU 247** run delivers **~125–130 KB/s** with **event occupancy ~28–35/event**
(model ~35), **bytes = callbacks × 244** (no loss), and **`fmd_arm=0`** (FORCE_MD unnecessary).
That is **~77% of the ~167 KB/s @150 µs model** — a **configuration-gate PASS but saturation-gate
FAIL**. The **residual cause is OPEN**: host ATT-buffer refill (ATT allocs block on `K_FOREVER`, so
`enomem=0` ≠ spare capacity) is a **plausible but unconfirmed** hypothesis; pool depth (48≈64) and
log cadence (1/5/10 s, ~28–30 variance) were both ruled out as the driver. The controller is not
exonerated either (full occupancy in some events; low-occupancy tail unexplained). The mandatory preflight for the open-FSU 2M sweep
(`open-fsu-benchmark-problem-statement.md` §6.6) is thus **not yet passed**; the FSU sweep is
**not run**. Detail per run below. **Takeaway: every "deep bottleneck" here has been a rig-config
artifact (echo, then MTU) — not the controller.**

2026-08-12 · 2× nRF54L15-DK · open Zephyr `fsu-m0` (HEAD 9999e040) · 2M · **50 ms interval
(confirmed 50000 µs both ends)** · DLE (resolved config; runtime effective length NOT logged — gap, see bounded claims) · downlink GATT write-without-response
(central→peripheral, the stable direction) · FSU **off** (150 µs, `APP_FSU_MAX_US=0`).

## Measured
- **Delivered goodput (sink `rxkBps`): ~24–26 KB/s**, steady over the window.
- **Central completion gaps `cgap` max ≈ 48000–60000 µs** — i.e. the **full 50 ms interval**;
  some writes wait an entire event. `enomem=0 err=0` — but note ATT allocs use `K_FOREVER`, so
  `enomem=0` does **not** imply spare buffers (a blocking alloc never returns ENOMEM).
- Build carried **no FORCE_MD** (`CONFIG_BT_CTLR_FORCE_MD_*` unset).

## Model (corrected per review — do NOT reuse the earlier "~80 µs/pair / 150+ KB/s" figures)
- Per packet-pair the 150→52 µs reduction saves **196 µs** (two IFS gaps/pair): frame time
  **1392 → 1196 µs**.
- A 50 ms event models **~35 pairs at 150 µs** and **~41–42 pairs at 52 µs**.
- Ideal delivered rate ≈ **167 KB/s at 150 µs** and **≈199 KB/s at 52 µs** (before
  implementation overhead). The observed ~24–26 KB/s is **~6.5× under** the 150 µs model.
- Independent **pump-headroom** requirement (≥20% over predicted radio throughput) therefore
  approaches **~240 KB/s** — recompute from the exact final frame mix before qualifying.
- Note: even a full 24-deep pool that recycles completions only *after* the event caps at
  ~24 × 244 B × 20 events/s ≈ **114 KB/s** — so **24 buffers are insufficient**; start at 48/64.

## Diagnosis — controller instrumentation kills the SPECIFIC early-close hypothesis (bounded)
Added a packets/event histogram + FORCE_MD arm counter to the controller
(`lll_conn-evfill-instrument.patch`; globals `lll_conn_q2_pe[16]` (later widened to `[64]` + max), `lll_conn_q2_fmd_arm`).
Re-run at 2M/50 ms, pool=24, no FORCE_MD (`evfill-q24-*.log`):

```
EVFILL events=291 avg_pe=12.3 fmd_arm=0 pe0-8=3/35/5/1/3/3/3/1/1 pe15+=219
```

- Events routinely carry **>14 transactions** (219/291 in the saturating 15+ bucket), and
  FORCE_MD **never armed** (`fmd_arm=0`). This **refutes the specific "FORCE_MD absent → only a
  few exchanges → early close"** hypothesis.
- **It does NOT prove the event fills the available airtime.** The histogram **saturates at
  15+**, while the model needs **~35–42 transactions/event** — so this only shows events
  *commonly exceed 14*, not that they reach the airtime limit. **Bug to fix: widen the
  histogram to 0–64 exact + record max + steady-state-only stats.**
- **FORCE_MD is untested for one-way**, not retired: in *this* bidirectional echo config the
  peer's return traffic can keep MD/event-continuation alive on its own. A clean one-way sink
  may behave differently — the FORCE_MD question stays open until the sink-only preflight runs.

**Proven confound (not yet the proven sole cause).** The peripheral log shows `blkkBps=0`,
`rxkBps≈24`, `pongs +1378/s`: `z54-lat`'s `APP_TPUT_BLAST` writes to the **ping characteristic
and the peripheral echoes every write** — a **bidirectional ping-pong flood**, not a clean
one-way downlink to a counting sink. The archived peripheral build **omitted the sink-only
overlay** (`CONFIG_APP_TPUT_SINK=y`, `z54-lat-periph/tput-open.conf`). The echo is a *confound*;
a **sink-only rerun is the discriminator** for whether it is the *sole* cause of the low rate.

## Sink-only run #1 (`sink-q24-*.log`) — VALID sink transport, but INVALID preflight (MTU 23)
Echo-off, pool 24. `EVFILL avg~42 max=100 fmd_arm=0`, sink `rxkBps≈18–20`, central `att≈ok≈470–517/s`.
**The apparent "6.6× sender-vs-sink gap" was NOT real — it was `MTU 23`.** The build negotiated
`MTU err=0 mtu=23` (both `.config` had `CONFIG_BT_L2CAP_TX_MTU=23`), so each write carried only
**20 bytes** (`bt_gatt_get_mtu()−3`). ~900–1650 callbacks/s × 20 B = the observed 18–32 KB/s
**exactly** — every write **delivered**, no loss, no RX cap, no retransmit problem. The max=100
histogram is likewise expected: ~100 short 20-byte exchanges physically fit a 50 ms event. So this
run is **valid sink-only transport but an invalid saturation preflight due to MTU 23**; the "new
bottleneck" was another rig-config artifact.

## Sink-only run #2 — MTU 247 (`mtu247-*.log`) — config-gate PASS, saturation-gate FAIL. Clean delivery.
Both endpoints rebuilt with `CONFIG_BT_L2CAP_TX_MTU=247` + DLE (`AUTO_DATA_LEN_UPDATE`), pool **48**,
echo off, FSU off. **Hard gate PASS:** interval `50000`, `PHY tx=2 rx=2`, `MTU err=0 mtu=247`,
sink-only, FSU off. (Binaries: `firmware/{central,periph}-mtu247.*`.)

```
EVFILL(ss=1) events=280 avg_pe=28.0 max=35 fmd_arm=0 | 0:8 1-4:19 5-14:28 15-29:27 30-41:198 42+:0
sink rxkBps≈123–143 (mean ~128)   periph ~554 cb/s × 244 B ≈ 132 KB/s (bytes ≈ callbacks × 244)
central att≈ok≈518–564 wwr/s, enomem=0
```

- **Delivered goodput ~125–130 KB/s** — up ~7× from the MTU-23 run; **bytes = callbacks × 244**,
  no loss.
- **Event occupancy contracted to avg ~28, max 35** (30–41 bucket ≈ 198/280, 42+ = 0) — right at
  the model's ~35 exchanges/event for 244-byte payloads, as predicted.
- **FORCE_MD still unnecessary** (`fmd_arm=0`): the deep queue + no echo fills the event on its own.

**Status: configuration-gate PASS, saturation-gate FAIL.** ~128 KB/s is **~77% of the ~167 KB/s
@150 µs model** and far under the ~240 KB/s pump-headroom target — "near-saturation" is generous;
the saturation gate is **not met**. The result is explained cleanly by **occupancy, with no
delivery gap**: 28 pairs/event × 244 B × 20 events/s ≈ **133 KB/s** ≈ delivered goodput.

**Limiter — cause OPEN (host ATT-refill is a plausible but UNCONFIRMED hypothesis).** Zephyr
allocates ATT TX buffers with **`K_FOREVER`**, so the 1.3–2.2 ms API call time *includes* any
buffer wait, and **`enomem=0` does NOT indicate spare capacity** (a K_FOREVER alloc never returns
ENOMEM). That makes host-refill plausible — but the ad-hoc pool (48≈64) and cadence (1/5/10 s)
tests did **not** confirm it, so the residual ~22% is **not localized**.

**Bounded claims (do NOT over-read):**
- `fmd_arm=0` proves FORCE_MD was **unnecessary to reach 35 pairs in *some* events**; it does
  **not** prove FORCE_MD could not raise the **28-pair mean**.
- The controller is **not "exonerated"** — it showed **full model occupancy in some events**;
  the cause of the **intermittent low-occupancy events** (the 0–14 buckets) is **open**.
- **Runtime effective DLE 251 was not printed** (only the resolved `.config`). The next run must
  add a `le_data_len_updated` record and **hard-gate the effective TX/RX lengths** — config alone
  is insufficient.

## Run 4 — pool 48 vs 64 (`mtu247-q64-*.log`) — DIAGNOSTIC. No material difference (my "+15%" was cherry-picked).
Rebuilt the central at `BT_BUF_ACL_TX_COUNT=64` (EVT_RX 68), identical otherwise. Using
**comparable broad windows** (not selected samples): occupancy **28.0 (48) vs 28.4 (64)** — no
change; delivered goodput **~133 (48) vs ~136 (64)** (raw incl. warmup ~119 vs ~112) — **within
noise, ~+2–3%, not +15%**. An earlier "128→148 (+15%)" comparison **selected different windows and
is retracted.** Sender still shows long submission stalls up to **~163 ms** (`okgap_max`) despite
`enomem=0` — consistent with ATT-buffer/refill blocking, but **does not prove pool depth is the
binding limit** (64 didn't help). **Occupancy stays ~28 < model 35 ⇒ saturation gate still FAILS;
FSU sweep NOT started.** Note: raising `BT_BUF_ACL_TX_COUNT` (the controller ACL pool) may be the
wrong knob — the write blocks upstream on the **L2CAP/ATT TX buffer** pool (`K_FOREVER`); that is
the next thing to test.

## Run 5 — cadence tests (`mtu247-q64-slowlog/log10s-*.log`) — INCONCLUSIVE (run-to-run variance)
The first-16 write calls are ~121 µs but **steady-state calls block ~1.6–1.8 ms** (min 1256 µs) —
once the (64-deep) pool fills, each write waits for a buffer the controller frees at its drain
rate. The occupancy histogram is **mostly full (30–41 bucket dominant) with a low-occupancy tail**
(0/1-4/5-14) and ~163 ms `okgap` stalls — and the per-second `printk` **runs inside the blast
loop**, blocking it. Controlled test (only the report cadence changed, **1 s → 5 s**, else
identical, pool 64):

```
1 s cadence:  avg_pe 28.4 ; goodput ~130 KB/s
5 s cadence:  avg_pe 30.5 ; goodput ~139 KB/s
10 s cadence: avg_pe 27.9 ; goodput ~129 KB/s
```

**CORRECTION (do not read a telemetry effect here).** An earlier note claimed the 5 s run
"confirmed telemetry perturbation." The **10 s run refutes that** — occupancy is **28.4 / 30.5 /
27.9** across 1/5/10 s, i.e. **~28–30 run-to-run variance with no monotonic cadence effect**.
Reducing the in-loop `printk` did **not** reliably raise occupancy or goodput. So the mechanism of
the low-occupancy tail is **NOT localized**: it is not the pool depth (48≈64), and it is not
cleanly the report cadence either. The pump sits at a **~28–30 occupancy / ~130 KB/s ceiling**,
**saturation-gate FAIL**, cause open.

**Lesson (self):** these ad-hoc single-run comparisons keep producing overclaims (the retracted
"+15%", now the retracted "telemetry confirmed"). The proper path is the reviewer's: **controlled
telemetry** (ATT pool-wait, queue depth at event boundary), **reset-isolated counterbalanced**
comparisons, **cumulative-byte deltas over identical windows**, and **RAM-aggregated / dump-after
-capture** logging — not more single-shot ad-hoc runs. FSU sweep remains **not started**.

## Corrected next step — the queue-depth discriminator (bounded; likely one cycle away, not proven)
1. At **each event boundary**, record **controller TX queue depth**, **ATT-pool availability**,
   and **ATT queue depth** (alongside the existing trx_cnt occupancy).
2. **Correlate low-occupancy events (trx<35) with queue-empty periods.** Decision rule: if the
   controller queue stays **nonempty while events stop below 35 → investigate the controller**;
   if it **empties before** low-occupancy events → optimize the **host ATT refill path**.
3. Add **runtime effective-DLE** logging; hard-gate eff TX/RX = 251.
4. **Aggregate counters in RAM, dump after capture** — per-second `printk` may itself perturb the
   pump (the low-event tail + occasional 50–100 ms call stalls may be telemetry artifact).
5. Run **48 vs 64 buffers with identical firmware logic**.
6. Measure **CPU idle**, but interpret it **separately from time blocked on ATT buffers**.
Still GATT (comparability preserved). Only after the saturation gate passes: the four-arm FSU sweep.

## What this run bounds (corrected)
**Nothing about downlink saturation.** With the peripheral echo enabled, ~24–26 KB/s is a
bidirectional ping-pong rate; it is an **invalid one-way preflight**, not a statement that the
path "cannot sustain long events."

*(The earlier FORCE_MD factorial / arm-threshold-decoupling plan is obsolete: the sink-only
MTU-247 run shows the controller reaches full occupancy in some events without FORCE_MD, so the
open question is intermittent low-occupancy, addressed by the queue-depth discriminator above —
not a FORCE_MD sweep.)*

## Provenance (all runs in this cycle)
Controller source = `fsu-m0` HEAD `9999e04044602404de40132ff21d32e30c81f858` +
`lll_conn-evfill-instrument.patch` (packets/event histogram `lll_conn_q2_pe[64]` + max +
`lll_conn_q2_fmd_arm`). App: `z54-lat` family, 50 ms conn param.
- **Run 1 — echo (invalid):** `pf24-*.log`, `evfill-q24-*.log` (pool 24, echo on, MTU 23).
- **Run 2 — sink-only MTU 23 (invalid):** `sink-q24-*.log` (echo off, pool 24, MTU 23).
- **Run 3 — sink-only MTU 247 (config PASS / saturation FAIL):** `mtu247-*.log`; **exact flashed
  binaries `firmware/{central,periph}-mtu247.{hex,elf,config}`**, app configs `central-prj.conf` /
  `periph-prj.conf`. Runs 1–2 used earlier builds (echo/MTU-23), superseded by run 3.
