# L2CAP CoC duplex FSU — independent traffic each way, matched arms, held FSU (2026-10-03)

**Status: RESOLVED (open + SDC, 7.5/15/25 ms).** First clean CoC duplex FSU measurement: the
reconnect wedge that blocked earlier attempts (`coc-duplex-fsu-interval-20260908`: open 0/10) was
avoided by never measuring the first CoC connection since the central booted.

## Results (aggregate = sink `SINK rx` downlink + central `CENRX` uplink slope, KB/s; Welch 95%)
| stack | interval | FSU off (n) | FSU on (n) | FSU gain | split dl/ul (off → on) | stalls |
|---|---|---|---|---|---|---|
| open (52 µs) | 7.5 ms | 169.2 (4) | 182.2 (4) | **+7.7% ± 1.1%**, strict non-overlap | 55/114 → 60/122 | 0 |
| | 15 ms | 169.3 (4) | 182.0 (3) | **+7.5% ± 1.1%**, strict non-overlap | 56/114 → 60/122 | 0 |
| | 25 ms | 163.8 (4) | 188.6 (4) | **+15.2% ± 4.3%**, strict non-overlap | 54/110 → 62/127 | 0 |
| SDC (65 µs) | 7.5 ms | 179.9 (4) | 179.6 (4) | −0.2% ± 1.2% (null) | 90/90 → 90/90 | 0 |
| | 15 ms | 182.3 (4) | 185.2 (4) | +1.5% ± 5.5% (null) | 91/91 → 93/92 | 0 |
| | 25 ms | 172.4 (4) | 200.4 (3) | **+16.2% ± 2.6%**, strict non-overlap | 86/86 → 100/100 | 0 |

Rejected reps: open 15 ms 1 (downlink FSU-held gate), SDC 25 ms 1 (uplink FSU-held gate). The SDC
25 ms cell comes from `sdc-25-rerun/`: the first SDC run (`sdc-full/`) crashed at 25 ms rep 4 when the
capture tool could not open the peripheral's serial port (cause not pinned down: the flaky USB serial on
that board, or the host idle-sleeping, which it did shortly after; the tool now rejects a missing capture
instead of crashing), so its 25 ms reps are not used.

## Reading (measured vs inferred)
- **Measured:** SDC CoC splits evenly and, like GATT echo duplex, gains nothing at 7.5/15 ms and gains
  at 25 ms. The open stack's CoC runs uplink-heavy (~1:2, the documented "freshly reset side wins"
  effect, because each rep resets the peripheral) and gains at every interval.
- **Inferred, not measured here:** with an uneven split many events pair a full packet with a short empty
  reply, so per-event packing is finer-grained than full exchanges and FSU's saved time is usable at any
  interval. The 25 ms CoC gains (+15–16%) exceed the GATT-echo +10% and the simple full-exchange model
  (10 → 11); the reason is open. CoC had no on-air observer check.

## Design (see REPRODUCE.md, "CoC duplex FSU (matched arms)"; tool `tools/coc-duplex-fsu.py`)
- Matched central arms: open `open-fsu.conf` vs `open-nofsu.conf` (differ only in `APP_FSU_MIN_US`);
  SDC `sdc-sel.conf;sdc-fsu.conf` vs `sdc-sel.conf;sdc-nofsu.conf`. `design-gate.txt`: all MATCHED.
- Sink `coc-duplex-sink`, open `open-fsu.conf` / SDC `sdc-sel.conf;sdc.conf`, both with
  `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` (checked in the resolved config).
- Per central image: flash + reset, discard the first (cold, wedge-prone) connection; each rep opens the
  capture, then resets ONLY the peripheral, so the measured connection is a same-boot reconnection whose
  FSU lines are recorded. A rep is rejected if the central rebooted, if FSU did not reach its arm's
  spacing (FSU-off: the 150 µs request only), if the interval drifted, if FSU did not hold, or if either
  direction stalled (counted separately; no stalls in any rep).
- Window: FSU completion (FSU-off: the request) + 2 s to the end of a 40 s capture.

Rig: 2× nRF54L15-DK (central 1057719509, peripheral 1057794857), close range, open `v4.4.2-16-gfee9fbc`,
SDC NCS v3.4.0. Files: per run `results.jsonl` + `caps/`; `firmware-*/` (+ SHA256SUMS), `configs-*/`.
SDC images are listed in THIRD-PARTY-NOTICES.md. Local paths scrubbed with `tools/scrub-paths.py`.
