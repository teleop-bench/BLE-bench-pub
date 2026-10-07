# Dedicated-lane (two-channel CoC) re-test — 2026-09-09

Re-runs the §11.3 "priority-lane" claim (`coc-latency-under-load-20260825`), which reported the
separate-channel stop-signal at **~33 ms (min 19 / max 69), single session** — a number that quoted the
*first* saturation window while its own log showed the control channel degrading over time.

**Rig:** `coclat2-central` (bulk PSM 0x0080 + control ping-pong PSM 0x0081) + `coclat2-sink`, open-fsu
(`EVENT_IFS_LOW_LAT_US=52`), pinned 15 ms, FSU `spacing=52`. Build recipe in REPRODUCE.md (§11.3); resolved
configs + firmware SHA archived here. Measures the **sustained-saturation window (t ≥ 10 s)**, n=8,
reset-isolated, gated (FSU held + bulk saturated + control channel not starved).

## Result (n=8, Student-t)
- **Control RTT mean: 32.2 ms ± 0.3** under a saturating bulk blast on the other channel
- **% over 30 ms: 100.0%** — essentially every stop-signal exceeds the 30 ms responsiveness target
- Worst spike: **87.1 ms**; control channel **never starved** (0/8; ~500 pings/window, 0 timeouts)

**Interpretation:** the dedicated lane is a real ~9× win over the shared channel (32 vs 290 ms) and is
*reliable* at that level (tight ±0.3, no starvation) — but it does **not** get under the tight 30 ms
responsiveness line; it sits just over it, 100% of the time. It is comfortably inside the real 300–700 ms
*stop* budget. Corrects the single-session "~33 ms clean pass" to "~32 ms, reliable, budget-borderline."
`harness.py <reps>` reproduces; `caps/` holds the raw LAT2 logs. **Firmware:** exact hexes in `firmware/`
(`coclat2-central.hex` + `coclat2-sink.hex`, SHA in `FIRMWARE-SHA256.txt`); controller pin
**`fee9fbc620959b218cda34602d7653d21f7f6a51` (v4.4.2-16)**. The reset-recovery-100 dir uses these same hexes.
