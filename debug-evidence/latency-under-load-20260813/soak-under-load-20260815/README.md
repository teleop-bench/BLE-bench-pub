# 25-hour soak under cycling load (2026-08-15) — reliability of today's paced safety-link config

Continuous soak of the **paced (completion-paced) load-ramp build** — today's exact
reviewed config — to qualify **link reliability under sustained load**. This is the
reliability dimension the throughput/latency panels don't cover: does the link stay up,
drop-free, and fault-free while bulk load cycles continuously for a day?

## Rig
2× nRF54L15-DK, 7.5 ms interval, 2M PHY. Central runs the serialized GATT stop-signal
ping-pong (8 B write → notify echo) **plus** the self-stepping bulk load ramp
(0→150 KB/s, 60 s/stage, `CONFIG_APP_LOAD_POLITE=y` = completion-paced to 1-outstanding).
So every stage from idle to saturation is exercised, repeatedly, for the whole run.

Provenance (`SHA256SUMS`): central `central-polite.hex` (472b3299…), periph `periph.hex`
(becfe6ac…) — the same binaries measured in `../hardening-20260814/`. `.config` preserved.

## Result — 25.15 h, authoritative (central counters)

| metric | value |
|---|---|
| duration | **90,557 s (25.15 h)** |
| round-trips | **4,923,931** |
| **timeouts / pong losses** | **0** |
| **disconnects** (both ends) | **0** (`disc=0x00` throughout) |
| **FATAL / faults** | **0** |
| write failures | **0** |
| observed availability | **100%** (down = 0 s, 1 Hz sampled) |
| load cycles (idle→sat) | **92** |
| worst single RTT (max-ever) | 94.4 ms (1 in 4.9M) |

**Zero disconnects, zero losses, zero faults across 4.9M round-trips and 92 continuous
idle→saturated load cycles over a day.** Nothing drops — the link stays up under sustained
cycling load. This upgrades the customer FAQ's "stays connected over hours/days" answer
from a vague *partial* to a concrete, provenanced ≥24 h number.

## What this does and does NOT establish
- **DOES:** continuous-uptime reliability under cycling load, clean RF, single connection,
  today's paced config. Exceeds the prior 4 h baseline soaks and adds the under-load dimension.
- **Does NOT (by construction):** degraded-RF/range/interference reliability, reset endurance,
  or multi-day rare-event rate bounds. Those need different rigs / longer runs — see the FAQ
  roadmap. A clean bench soak, however long, cannot answer the range/RF question.

## Tail-latency caveat (read before quoting a percentile)
The cumulative bucket counts (>15 ms 42%, >20 ms 8.0%, >30 ms 0.26%) and the derived
conditional p99.9 (">30 ms") are **whole-run, mixed across all load stages** — dominated by
the saturated (125–150 KB/s) stages. This is NOT a fixed-load latency tail. For the
per-load-level paced tail (p99 ~26–28 ms at saturation), see `../hardening-20260814/`
(n=2 per-stage percentiles). The soak's contribution is **reliability**, not a tighter
latency number.

## Logging note (not a link event)
The periph's serial *console capture* twice stalled on a host-side USB re-enumeration
(`soak-periph-part1` ended ~3.35 h, `part2` ~13 h). The **periph board never dropped** —
the central's round-trip counter (a round-trip completes only when the periph echoes) climbed
to 4.9M with 0 timeouts, and the periph's own last-seen counter (pongs 2.2M, disc=0x00) agreed.
The central log is the authoritative record; the periph console gaps are host-USB artifacts.

## Files
`logs/soak-central-full.log` (authoritative, 25 h), `logs/soak-periph-part{1,2}.log` (partial,
host-USB-truncated), `central-polite.hex` / `periph.hex` / `central-polite.config`, `SHA256SUMS`.
