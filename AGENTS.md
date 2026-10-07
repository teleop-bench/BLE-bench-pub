# AGENTS.md — orientation for coding agents

## What this is
A reproducible Bluetooth LE 5/6 throughput & latency benchmark — open Zephyr `ll_sw_split` (patched)
vs Nordic SoftDevice Controller — for a robotics teleop Wi-Fi-failover safety link.
Start at [README.md](./README.md).

## Map
- `apps/{nrf54l15,nrf52,z44,coc,eatt,esp32,misc}/` — firmware projects
- `docs/{overviews,plans,investigations,references,legacy}/` — write-ups, plans, findings narrative
- **[`docs/EVIDENCE-INDEX.md`](docs/EVIDENCE-INDEX.md) — the ledger of every result: its true status (accepted / confirmed / quarantined / retracted / open) and which `debug-evidence/` dir holds the proof + recipe. READ IT before claiming a result is measured/done, or before re-running one** (it maps the README headlines to their dirs; 2M/52 is accepted and its promotion record lives there)
- **[`docs/LESSONS.md`](docs/LESSONS.md) — pitfalls / retractions / gotchas registry. READ IT before running or interpreting a benchmark** (so you don't re-sweep `debug-evidence/` or re-assert a disproven claim)
- `tools/` — host tooling: `capture-tool.py` (serial capture, DTR-aware), `analyze.py` (parse capture logs → headline KB/s / RTT; **auto-detects every rig's line format** — CoC & GATT throughput, **uplink/duplex via `CENRX`**, latency-under-load, RTT-ramp — plus the interval/PHY/DLE/FSU sanity flags)
- `run-campaigns.sh` — runs every current campaign below in one unattended pass (`--dry-run`, `--smoke`, `--only`); `run-all.sh` is the archival August smoke test
- `tools/` (per-result campaigns) — one-command matched-arm tools: `gatt-duplex-fsu.py`, `coc-duplex-fsu.py`, `latency-fsu.py`, `oneway-fsu.py` (Zephyr vs SDC, same session), `onair-duplex.py`, `observer-smoke.sh`; `scrub-paths.py` (reversible path scrub + `verify` for SHA256SUMS); `link_gates.py` (per-rep runtime gates the tools apply: PHY, data length, interval held, CoC credit window). Use these rather than new harnesses.
- `safety/` — the dead-man's-switch fail-safe STOP demo (heartbeat → external WDT → STOP)
- `prebuilt-hexes/` — flash-and-go firmware + `SHA256SUMS`
- `zephyr-patches/fsu-m0-series/` — the 16 controller patches (+ provenance)
- `debug-evidence/` — raw logs, resolved `.config`, per-experiment `FINDINGS` + manifests

## Build / flash / capture / analyze
**It's all in [REPRODUCE.md](./REPRODUCE.md) — go there; don't restate commands here.** In short:
patched Zephyr = fork `github.com/teleop-bench/zephyr` — branch `fsu-m0` (v4.4.1 line, turnkey `west init -m … --mr fsu-m0 && west update`) and `fsu-m0-v442` (v4.4.2-16, used by the September+ campaigns); pin the evidence-specific commit;
two paths (flash `prebuilt-hexes/`, or `west build` the recipes). **Reproduction needs the physical rig
(2× nRF54L15-DK + an nRF52 observer) — no agent reproduces the RF numbers without hardware.**

## Non-negotiable disciplines (why this file exists — you can't infer these from the code)
1. **Never overclaim; retract on evidence.** Findings carry hedges / CIs / provenance — preserve them; don't round a caveated result into a clean headline.
2. **Reproduce *deltas*, not absolutes.** Absolute KB/s and RTT are RF-day-specific; the FSU on/off gain and open-vs-SDC comparison are what transfer.
3. **Log the operating point** (interval / SDU / spacing / distance / PHY / DLE) on every capture, and only compare like-for-like. Un-pinned comparisons manufacture false effects (see REPRODUCE gotchas).
4. **Validate with the sanity-gates** (REPRODUCE.md "Sanity checks") before trusting any number — most failure modes here don't error out, they hand you plausible-but-wrong data.
5. **Units: KB/s = 1024 bytes** in reader-facing docs — never "KiB".
6. **`debug-evidence/**` and any `PROVENANCE.*` are frozen historical records — do not edit** (SHAs and numbers are as-measured; boot banners intentionally cite pre-publication SHAs). One sanctioned exception: for publication, the author's local home-directory paths were replaced with placeholders (`<REPO>`, `<ZEPHYR_WS>`, `<NCS_WS>`, `<ZEPHYR_SDK>`, `<HOME>`) by `tools/scrub-paths.py`. The substitution is exactly reversible, so verify any recorded file hash with `python3 tools/scrub-paths.py sha256 <file>`, not a plain `shasum`, and run tools that re-hash archives (the observer ABBA combiners) on a `tools/scrub-paths.py unscrub-tree` copy.
7. **REPRODUCE.md is a how-to, not a results doc.** Results, findings, resolved items, and open questions live in `docs/overviews/` — put them there, not in REPRODUCE.
8. **Config via `-D` Kconfig / `EXTRA_CONF_FILE` overlays, never source edits** for interval/SDU/FSU knobs. Controller changes go in the `fsu-m0` fork, exported to `zephyr-patches/fsu-m0-series/`.
9. **Use the existing host tooling — don't reinvent it.** For a result that has a campaign tool (above), use it; otherwise capture with `tools/capture-tool.py`, parse with `tools/analyze.py` (it already handles all the rig line-formats above + the sanity flags). Check these before writing any new parser or capture script.
10. **Read [`docs/LESSONS.md`](docs/LESSONS.md) before designing or interpreting a run** — check the retractions so you don't re-assert a disproven claim or repeat a known measurement trap; when a run establishes or retracts a lesson, add/flip it there (one line + pointer).
11. **Never state a result's status or completeness from memory.** Before you claim something is measured / accepted / done — or decide to re-run it — find its row in [`docs/EVIDENCE-INDEX.md`](docs/EVIDENCE-INDEX.md) and open the cited `debug-evidence/` dir. Memory and one-line summaries are lossy and have caused repeated "we missed a dir" errors. Every new `debug-evidence/` dir adds a row to the index in the same change; supersession flips the older row.
