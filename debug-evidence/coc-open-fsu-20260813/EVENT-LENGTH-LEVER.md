# Event-length lever investigation: why FSU gives 0% at 7.5ms, and the interval that unlocks it

## Question
At 7.5 ms the CoC event caps at 5 packets and FSU (tIFS 150→52 µs) gives ~0%. Is that a
fixable controller early-close (so FSU could convert at 7.5 ms), or a real floor?

## Code mechanism (Nordic open ll_sw_split, agent-verified against source)
- **No tIFS-aware "will the next packet fit?" check exists.** The LLL chains packets blindly:
  the continue/close decision (`nordic/lll/lll_conn.c:874-883`, in `lll_conn_isr_rx`) reads ONLY
  the MD (more-data) bits + CRC. Under a saturated queue the central's `tx->md` is always 1
  (`:1461-1466`), so this never closes the event.
- **The event is torn down by the NEXT connection anchor's prepare**, `lll_conn_central_is_abort_cb`
  → `-ECANCELED` once `trx_cnt>=1` (`:535-562`, `:602-635`). That preemption fires at a **fixed,
  interval-derived, tIFS-INDEPENDENT** offset (~`EVENT_OVERHEAD_START_US`=733 µs + ready-delay
  before the next anchor). `lll->tifs_tx_us`/`tifs_rx_us` (which FSU updates, `ull_conn.c:2887-2932`)
  are used ONLY to program the radio switch — never to decide continuation.
- `ticks_slot` (`ull_conn.c:2409-2419`) reserves just ~one transaction and the event is allowed to
  overrun it; it is not the cap. FORCE_MD/AUTO only hold the event against queue *starvation*
  (moot here) and their keep-alive math even hardcodes 150 µs (`:2173-2218`).
- **Verdict: the 5-packet cap at 7.5 ms is a genuine airtime/quantization floor, NOT a fixable
  early-close.** FSU shrinks each transaction (~0.19 ms saved) but nothing reclaims it; the freed
  ~0.95 ms over 5 transactions is < one ~1.2 ms transaction, so no 6th packet opens. No termination-
  logic knob can convert it. The only lever that admits more packets is a LARGER USABLE WINDOW
  (i.e. a longer interval), or lower per-transaction overhead.

## The lever is CONNECTION INTERVAL (interval sweep, 480 B SDU, receiver-delivered goodput)

| interval | no-FSU (f150) | FSU (f52) | FSU gain | packets/event (off→on) |
|----------|---------------|-----------|----------|------------------------|
| 7.5 ms   | ~142.5 (145.9,139.2) | ~145.3 (146.5,144.1) | **+2 % — overlap, NONE** | max 5 → 5 |
| 15 ms    | ~142.5 (141.5,143.4) | **~166.0 (172.8,164.0,161.2)** | **+16.5 %** | max 10 → 12 |
| (50 ms†) | 131 | 150 | +14 % | max 34 → 40 |

† 50 ms row used 244 B SDU (different regime) — shown for trend only.

- no-FSU is FLAT ~142–146 across 7.5–15 ms (its airtime ceiling at tIFS=150 µs).
- FSU's benefit GROWS with interval because more packets/event means its ~14 % per-transaction
  saving stops being lost to integer-packet quantization (7.5 ms: 14 % of 5 < 1 packet → 0 gain;
  15 ms: 10→12 packets → realized).
- **FSU @ 15 ms (166) EXCEEDS the no-FSU peak (145.9 @ 7.5 ms) by ~+14 %.** n=3 FSU runs all > all
  no-FSU runs; 0 disconnects.

## Corrected conclusion (supersedes BOTH earlier claims)
1. My first "+20 % beats open" was regime-blind (it was a 50 ms number).
2. My RECONCILIATION.md over-corrected to "FSU does not raise the ceiling" — that was because I only
   compared 7.5 ms (quantization-blocked) and 50 ms/244 B (150 < 157). I missed the ~15 ms sweet spot.
3. **CORRECT, data+code-backed: FSU RAISES the achievable CoC throughput ceiling by ~+15 %, realized
   at a moderately longer interval (~15 ms), where it reaches ~166 KB/s and beats plain-Zephyr's
   best (~145). At the conventional 7.5 ms interval FSU gives ~0 % (airtime quantization — only 5
   packets/event). So "open + FSU · CoC · no gain" is true ONLY at 7.5 ms; at 15 ms FSU delivers a
   real, ceiling-raising +15 %.** The lever is the connection interval, not a controller code change.

Caveats: same-day RF (absolute levels ~14 KB/s below the charted 157 — see RECONCILIATION.md);
the ~15 ms optimum is bracketed by 7.5 and 15 ms, not finely swept; interval trades latency for this
throughput (15 ms vs 7.5 ms). Evidence: `event-length-sweep/` (6 arms @15 ms) + `reconcile-7p5ms/`.
