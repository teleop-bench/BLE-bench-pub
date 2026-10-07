# Root cause: why the August +14.4% CoC one-way FSU gain doesn't reproduce — 2026-09-08

**Question:** `coc-open-fsu-20260813` measured **+14.4%** CoC one-way FSU (50 ms/244 B, off 131.3 → on
150.2 KB/s, occ 28→34). The n=60 campaign + interval sweep (2026-09-07/08) measure **~0% at the same and
every interval** (~160, occ ~33). Config lever, controller change, diagnostic artifact, or RF/session?

**Method:** eliminate every firmware/config/version hypothesis with the strongest possible tests — flash
the *exact archived August binaries* and a *faithfully-rebuilt v4.4.1 sink* today. All from-boot, ABBA.

## Hypothesis scorecard (all resolved on hardware)
| # | Hypothesis | Verdict | Evidence |
|---|---|---|---|
| 2 | `CONFIG_BT_CTLR_FSU_EVENTFILL_DIAG` hot-path throttle | ❌ REFUTED | DIAG=y ≈ DIAG=n ≈ 158, FSU −0.5% both; throttle +0.2% (`diag-refutation.txt`). Kconfig claims ~20%; HW shows ~0%. |
| 3 | Central controller version (v4.4.1 → v4.4.2) | ❌ REFUTED | exact August v4.4.1 central hex runs **~154–156** today (`august-hex-todaysink.txt`), ~same as v4.4.2 builds |
| 4 | Config / SDU / interval | ❌ REFUTED | interval sweep flat 15/25/37.5/50 ms; exact August 50 ms/244 B config → ~0% |
| 5 | Sink controller version (v4.4.1 vs v4.4.2) | ❌ REFUTED | v4.4.1 central + **v4.4.1 sink** (faithful stack) → **~156, occ ~33, FSU ~0%** (`august-hex-v441sink.txt`) |
| 1/6 | **Below-ceiling session / physical environment** | ✅ **ROOT CAUSE** | by elimination + the occ fingerprint below |

## The decisive fingerprint
The **identical software** (bit-for-bit August central hex + faithfully-rebuilt v4.4.1 sink + same configs)
fills to **occ ~33 pkts/ev / ~156 KB/s today**, but filled to **occ 28 / 131 KB/s in August**. Fewer
transmissions-per-event with *identical bits* ⇒ the sender was **starved by something external to the
software** — the physical/RF conditions of the August session (ambient 2.4 GHz / setup / distance),
which are unrecoverable and are **not a config knob**. occ 28 < the ~34-PDU airtime ceiling at 50 ms =
headroom; FSU (tIFS 150→52 µs) filled it → +14.4%. Today occ 33 ≈ ceiling = no headroom → FSU flat.

## Root cause + general principle
**The +14.4% was a below-ceiling *session* artifact, not a reproducible CoC-FSU property.** The general
law (now airtight): **CoC one-way FSU converts to goodput only when the link sits BELOW its airtime
ceiling; at the ceiling — the normal clean-bench state — it is a no-op.** A positive CoC one-way FSU
number is only as real as its baseline: a depressed baseline (August 131, or the low-baseline sweep
points) inflates the delta; an at-ceiling baseline (clean bench ~156–160) yields ~0%.

## Implications for the headline numbers
- **CoC one-way FSU: honest reproducible number is ~0% at the ceiling.** The +14.4% / +9–24% figures
  were below-ceiling session artifacts — do NOT present as a reproducible one-way CoC gain.
- **GATT one-way FSU +20% is robust** (at-ceiling, n=60 counterbalanced) — keep as the FSU headline.
- FSU is still real & valuable *where the link is airtime-bound with headroom* (verified on air 150→52 µs;
  helps GATT; raises the ceiling in loaded/duplex/longer-interval regimes) — the point is it does not lift
  a one-way CoC link that is already at its clean-bench ceiling.

## Meta-lesson
Two compelling forensic hypotheses (RF-day; then `EVENTFILL_DIAG`, backed by the controller's own Kconfig
note + 131×1.22≈160 arithmetic) were BOTH wrong — settled only by flashing the exact archived binaries.
Keep the actual firmware hexes archived per result; they enable a definitive firmware-vs-environment
split that no amount of config forensics can. Report a throughput delta only WITH its baseline vs the
airtime ceiling.
