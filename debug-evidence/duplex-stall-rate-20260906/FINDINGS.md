# Independent CoC-duplex uplink-stall RATE, by interval (2026-09-06)

First deliberate **stall-rate** measurement (prior work was anecdotal fractions). 12 reps each at
7.5 ms and 25 ms, reset-to-re-roll between reps. Open `ll_sw_split`, fork `8f44a3d7a3f`, FSU-on,
2× nRF54L15 (cen `1057794857` / periph `1057719509`), one board pair, one RF setting, 30 s/rep.
Raw: `captures/*.log`. Scored with `tools/analyze.py` (downlink = sink `CoC throughput`, uplink =
central `CoC uplink (CENRX)`).

## Result
| interval | balanced | stalled | dead | balanced per-direction |
|---|---|---|---|---|
| **7.5 ms** | **12/12** | 0/12 | 0 | ~77 down + ~78 up (≈156 aggr, imbalance ~1.8%) |
| **25 ms**  | **2/12**  | 10/12 | 0 | ~76 down + ~77 up (≈153 aggr, imbalance ~1.6%) |

Scoring rule (corpus): balanced = both directions flowing; stall = downlink healthy + uplink 0;
dead = both 0 (none here). No byte-identical captures (dedup check passed).

## Findings
1. **Interval GATES the uplink stall.** 7.5 ms: uplink comes up **every rep** (0/12 stall). 25 ms:
   uplink starves **10/12**. This **confirms** the app-author hypothesis (`coc-duplex-central/src/main.c:30`,
   "worse at longer intervals") — previously just a code comment — and matches `tx-staging-20260826`
   ("7.5 ms flowed, no stall").
2. **When balanced, it's symmetric at the shared-airtime ceiling:** ~77 each way, ~1.7% imbalance,
   ~155 aggregate — i.e. **~77+77, NOT 90+90** and NOT full-rate-each. Confirms the airtime picture.
3. **Refines "intermittent, not config-deterministic":** interval is a strong controlling variable —
   the stall is *absent* at 7.5 ms and *intermittent* (10/12) at 25 ms. Not purely deterministic
   (25 ms still gave 2 balanced), but interval-gated.
4. **Nuance vs `coc-duplex-artifact-20260814` ("symmetric full-rate CoC-duplex unreachable"):** no hard
   contradiction — symmetric **half-rate-each** (~77+77) IS reliably reachable at 7.5 ms; symmetric
   **full-rate** (each ~150) is not (one connection's airtime is shared).

## Caveat — CONFOUND (not yet isolated)
This run reset **both** boards each rep → every rep is the central's **first CoC since boot**, which is
also the condition for the separate **reconnect wedge** (`uplink-coc-diag deconfound-v3`). So the 25 ms
stalls conflate the reconnect wedge with steady-state head-of-line starvation. The **periph-only-reset
isolation** (central stays booted → subsequent same-boot connection = steady-state) disambiguates:
if 25 ms balances when the central isn't rebooted → the stall was the first-boot wedge; if it still
stalls → steady-state starvation. (7.5 ms already balances 12/12 even cold, so the wedge does not
manifest there.)

## Isolation result — the 25 ms stall is the RECONNECT WEDGE, not steady-state starvation
Periph-only reset (central stays booted → subsequent same-boot connection), 25 ms, 12 reps
(`captures-iso/`, `iso_run.sh`): **uplink never stalled — 0/12** (vs 10/12 stalled in the cold-reset
reset-both run). So the 25 ms uplink stall is the **first-CoC-since-central-boot reconnect wedge**
(`uplink-coc-diag deconfound-v3`), which vanishes once the central stays booted — it is **not**
steady-state head-of-line starvation.
- **New observation (not root-caused):** the subsequent-connection balance is *reversed & asymmetric*
  — **uplink ~110 / downlink ~55 (~170 aggregate)**, consistent across all 12. I.e. the freshly-rebooted
  side's TX dominates and the peer that stayed up is throttled (plausibly the central's downlink
  recovering credits/buffers after the peer-triggered reconnect). Clean symmetric ~77/77 appeared only
  at 7.5 ms.
- **Net:** at 25 ms, steady-state duplex uplink is healthy; the "stall" is a reboot/reconnect artifact,
  not an airtime/credit ceiling.

## Method lesson (added to docs/LESSONS.md)
A **balanced** duplex rep has **downlink ~75, not ~150** (it drops to share airtime with the uplink).
A validity gate of "downlink > 100 = healthy" WRONGLY excludes balanced runs — it flagged all 12
balanced 7.5 ms reps as "dead" on the first pass. Score balance on *both counters flowing*, not on
downlink magnitude.
