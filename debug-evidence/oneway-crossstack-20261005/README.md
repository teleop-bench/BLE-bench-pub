# One-way FSU, open Zephyr vs Nordic SDC: same session, equal tuning (2026-10-05)

**Status: RESOLVED. The cross-stack reference for one-way throughput.** Both stacks and both transports
were measured in **one session, interleaved, with one verified tuning profile**, so Zephyr-vs-SDC absolute
rates are directly comparable here (earlier campaigns measured each stack in its own session). 192 reps,
191 accepted (sweep 160/160). Placement: `PLACEMENT.md` (~8–10 cm apart, close range).

## 1. Tuning headroom: the recipes were not buffer-limited (`headroom.txt`, FSU on, n=2)
| FSU on | 7.5 ms: recipe → max tuning | 50 ms: recipe → max tuning |
|---|---|---|
| GATT Zephyr | 189.2 → 190.0 (+0.4%) | 191.8 → 193.0 (+0.7%) |
| GATT SDC | 158.0 → 158.0 (0%) | 185.0 → 185.0 (0%) |
| CoC Zephyr | 157.0 → 157.0 (0%) | 184.0 → 184.2 (+0.1%) |
| CoC SDC | 126.0 → 125.5 (−0.4%) | 181.5 → 182.0 (+0.3%) |

"Max" = SDC TX/RX packet count 20 (its maximum) and 50 ms connection events with extension on both boards;
open controller RX buffers 18 (its maximum) on both boards; 20 host ACL TX buffers on GATT senders (CoC
senders already use 64), 20 extra host ACL RX buffers on receivers. **Max tuning changes throughput by
≤ 0.7% anywhere**, so SDC's lower rates at short intervals are controller behaviour, not configuration.
This retracts the earlier "September GATT SDC builds not fully tuned / absolute rates may be understated"
caveat. The sweep uses "max" for both stacks.

## 2. Sweep (`summary.txt`; KB/s receiver-delivered, FSU off → on, n=4 per cell, Student-t 95%)
| transport | interval | Zephyr off → on (gain) | SDC off → on (gain) | Zephyr − SDC, FSU off | Zephyr − SDC, FSU on |
|---|---|---|---|---|---|
| GATT | 7.5 ms | 157.5 → 189.4 (+20.2% ± 1.9) | 127.0 → 158.0 (+24.4% ± 0.0) | **+30.5 ± 1.6** | **+31.4 ± 1.5** |
| | 15 ms | 158.0 → 189.6 (+20.0% ± 0.5) | 142.0 → 174.0 (+22.5% ± 0.0) | **+16.0 ± 0.0** | **+15.6 ± 0.8** |
| | 25 ms | 157.9 → 189.2 (+20.1% ± 9.9)* | 161.4 → 180.5 (+11.9% ± 1.7) | −3.5 ± 12.3* | **+8.8 ± 3.3** |
| | 37.5 ms | 164.4 → 195.5 (+18.9% ± 0.7) | 164.6 → 183.8 (+11.6% ± 0.3) | −0.2 ± 1.0 | **+11.8 ± 1.4** |
| | 50 ms | 165.0 → 192.4 (+16.6% ± 3.1) | 162.0 → 184.5 (+13.9% ± 0.7) | +3.0 ± 3.7 | **+7.9 ± 4.1** |
| CoC | 7.5 ms | 157.0 → 157.0 (+0.0% ± 0.0) | 125.5 → 125.5 (+0.0% ± 0.9) | **+31.5 ± 0.6** | **+31.5 ± 0.6** |
| | 15 ms | 156.9 → 188.4 (+20.1% ± 0.7) | 141.0 → 157.0 (+11.3% ± 0.0) | **+15.9 ± 0.4** | **+31.4 ± 1.2** |
| | 25 ms | 159.5 → 187.4 (+17.5% ± 1.7) | 150.9 → 178.8 (+18.5% ± 0.2) | **+8.6 ± 1.8** | **+8.6 ± 1.4** |
| | 37.5 ms | 162.4 → 187.2 (+15.3% ± 0.9) | 156.4 → 181.9 (+16.3% ± 0.8) | **+6.0 ± 2.2** | +5.4 ± 3.0 |
| | 50 ms | 164.2 → 186.5 (+13.6% ± 3.4) | 158.1 → 182.9 (+15.7% ± 3.2) | **+6.1 ± 1.8** | +3.6 ± 6.2 |

\* One rep (round 3, Zephyr FSU off) read 147 KB/s against 161–162 in the other rounds: a single radio dip
that passed the gates (the FSU-held gate runs on FSU-on reps only). Kept per protocol. Without it the cell
is 161.5 → 189.2 (+17.3%) and Zephyr − SDC (FSU off) ≈ 0. No other cell is affected.

## Reading (measured)
- **Zephyr is ahead at short intervals on both transports**, by ~30 KB/s at 7.5 ms and ~16 KB/s at 15 ms
  (CoC with FSU: ~31 at 15 ms), with and without FSU. From 25 ms the stacks converge: without FSU GATT is
  at parity (−3.5 to +3.0 KB/s) and CoC Zephyr +6 to +9 KB/s; with FSU Zephyr leads by +4 to +12 KB/s (its 52 µs gap vs SDC's 65–70 µs).
- **FSU gains replicate the September campaigns:** GATT ~17–20% (Zephyr) and 12–24% (SDC) at every
  interval; CoC ≈0% at 7.5 ms on both stacks, then 11–20%.
- Negotiated gaps: Zephyr 52 µs; SDC 70 µs on GATT, 65 µs on CoC.
- Same session, one placement, one rig: absolute rates still depend on the RF environment; the
  cross-stack differences above are the transferable result for this pair of controllers and recipes.

## Design (`tools/oneway-fsu.py`; REPRODUCE.md, "One-way FSU, Zephyr vs SDC, same session")
- Recipes per transport × stack as documented (GATT 244 B writes; CoC SDU 480 B), plus the tuning profile
  as `-D` flags. 64 images (`firmware/` + SHA256SUMS, `configs/`): 40 sweep centrals + 4 sinks (max), 16
  headroom centrals + 4 sinks (recipe tuning).
- Design gates before any flashing (`design-gate.txt`): on/off arms differ only in the requested frame
  space (matched pair); the tuning profile present in the resolved configs of both boards; **host parity**:
  the Zephyr and SDC images of the same transport agree on every host setting (ACL TX/RX buffers and sizes,
  L2CAP TX MTU/buffers, event RX buffers, SDU, interval, data-length update); FSU-capable sinks with
  connection-parameter auto-update off.
- Order: every round visits all 40 cells with Zephyr and SDC adjacent in time; even rounds reversed.
  Per rep: flash if changed, open the capture, reset both boards, 32 s capture.
- Per-rep gates: FSU on requires a reduced spacing logged (52 µs on Zephyr); FSU off requires the 150 µs
  request and no reduced spacing; `tools/verify_run.verify` (FSU held, interval, live link); throughput =
  median over the last 12 s, starting ≥ 1 s after FSU took effect.
- `smoke-20261005/`: the pre-run smoke at the earlier, wider placement (25 ms, one round). It showed the
  pipeline working but rates ~20% low with large per-second swings (packet loss), which is why the boards
  were moved back to close range before this campaign. Not part of the result.

Files: `PLACEMENT.md`, `results.jsonl`, `summary.txt`, `headroom.txt`, `design-gate.txt`, `run.log`,
`caps/`, `firmware/`, `configs/`, `smoke-20261005/`. SDC images are listed in THIRD-PARTY-NOTICES.md.
Local paths scrubbed with `tools/scrub-paths.py`. Open `v4.4.2-16-gfee9fbc`; SDC NCS v3.4.0.
