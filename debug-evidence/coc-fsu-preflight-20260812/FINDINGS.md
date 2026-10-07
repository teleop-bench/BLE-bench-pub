# CoC FSU-off saturation preflight — 2026-08-12

**Question (pre-registered):** Does an L2CAP CoC downlink pump saturate the 2M / 50 ms
link to the airtime model (~35 packets/event, ~167 KB/s), or does it cap at the same
~130 KB/s the GATT write-without-response pump was stuck at?

**Pre-registered interpretation:**
- CoC saturates (~model) ⇒ the GATT ceiling was **host-path-specific** (GATT/ATT layer).
- CoC also caps ~130 KB/s ⇒ the ceiling is **architecture-independent** (a deeper limit —
  controller event-packing / CE-length / refill timing) ⇒ the reviewer's "Option 3" signal.

## Rig

- **Downlink**: nRF54L15-DK central (`z54-central` CoC blaster) → nRF54L15-DK sink
  (`z54-sink` seg_recv). PSM 0x0080. Both on Zephyr 4.4.1 `fsu-m0` (HEAD 9999e040),
  controller occupancy instrument `lll_conn_q2_pe[]` live.
- **Held constant vs the GATT preflight**: ACL_TX=64, EVT_RX=68, FORCE_MD=0 (default),
  2M, DLE 251, 50 ms, FSU **off**. Console forced to raw synchronous printk (`LOG=n`)
  after `LOG=y` deferred logging was found to swallow output under BT load.
- SDU 244 B → 246 B L2CAP payload (244 + 2 B SDU-length) → **one** K-frame segment
  per SDU (MPS 247). So on-air it is one ~246 B PDU per SDU — essentially the same
  per-PDU airtime as the 244 B GATT write. CoC airtime ceiling ≈ GATT ceiling ≈
  ~35 pairs/event @150 µs tIFS ≈ **~167 KB/s**.

## Runtime gates (clean session — all PASS)

```
central  DLE: tx_max=251 rx_max=27
central  L2CAP connected: tx.mtu=512 tx.mps=247 rx.mtu=245 rx.mps=247
central  GATE conn: interval=40 => 50.00 ms
sink     GAP connected: interval=40         (50 ms)
sink     PHY updated: tx=2 rx=2             (2M)
sink     DLE updated: tx_max=27 rx_max=251  (sink receives 251-octet PDUs)
sink     CoC up — seg_recv sink ready (RX_CREDITS=64, MPS=247)
```

## Result — 3 sessions, incl. 2 reset-isolated reps with the clean K_FOREVER pump

Goodput = sink cumulative-byte Δ over a fixed window; segment count corroborates
independently (segs × 244 B) to the decimal in every case.

| session                              | pump      | goodput      | occupancy mean (max) | eagain | poolfail |
|--------------------------------------|-----------|:------------:|:--------------------:|:------:|:--------:|
| initial (K_NO_WAIT accounting build) | K_NO_WAIT | 121.9 KB/s   | ~25 (35)             | 0      | high*    |
| **rep 1** (K_FOREVER, 60 s)          | K_FOREVER | **122.7 KB/s** | **26.1 (35)**      | **0**  | **0**    |
| **rep 2** (K_FOREVER, reset-isolated fresh session, 54 s) | K_FOREVER | **119.3 KB/s** | **25.4 (34)** | **0** | **0** |

*rep 2 is a genuinely fresh connection (central `sent` counter restarted near zero
after a pin-reset), so this is a reset-isolated replication, not a re-window.

Rep 1 per-window occupancy: 25.0 / 26.0 / 26.4 / 25.2 / 27.3 / 26.7.
Rep 2 per-window occupancy: 25.0 / 27.7 / 27.1 / 23.7 / 23.5.

### What `eagain`/`poolfail`/`trx_cnt` do and do NOT prove (reviewer corrections)

The instruments are weaker than I first claimed. Do **not** read causality into them:
- **`poolfail=0` is guaranteed by construction, not evidence.** The K_FOREVER pump
  blocks in `net_buf_alloc` instead of failing, so `poolfail` is 0 by definition;
  allocation **wait time** was never measured. "Pump never starves" is **unsupported** —
  retracted.
- **`eagain=0` does NOT prove credits are non-binding.** Zephyr's dynamic CoC send path
  *queues* an SDU when credits are unavailable and returns success — it does not return
  `-EAGAIN`. With no `.sent`/outstanding accounting or controller-queue-depth, "credit
  window never binds" / "deep downstream backlog" are **unsupported** — retracted.
- **`trx_cnt` records the transaction count when an event ends** — not *why* it ended,
  nor whether another payload was queued and ready, and it counts LL transactions, not
  necessarily distinct delivered CoC SDUs. So "the controller closes at ~26 pairs" /
  "controller-limited" / "architecture-independent ceiling" are **not established** —
  retracted.

## Reading (bounded)

- **The tested CoC pump delivered ~121 KB/s** (119–123 across 3 sessions), and events
  ended at **~25–26 LL transactions on average** (occasionally reaching 34–35), well
  below the ~35/event airtime model (~167 KB/s). **The limiting layer is UNRESOLVED.**
- **Both open-Zephyr downlink pumps — GATT and CoC — stayed below the modeled airtime
  ceiling at 2M/50 ms (~121–130 KB/s).** Neither experiment localized the limiting
  layer. (The pre-registered "architecture-independent ceiling" reading is NOT
  supported by this instrumentation; that would need the measurements listed below.)

## Serial-capture corrections (this session)

Three read-path issues cost most of the session and one earlier conclusion:
1. **`CONFIG_LOG=y`** routes `printk` through a deferred log thread that swallows
   output under BT load → use `LOG=n` for raw synchronous printk.
2. **`tcsetattr` in the reader corrupts** the macOS cu.* CDC stream → configure the
   port only via external `stty ... raw`, never `tcsetattr`.
3. **DTR** — the nRF DK J-Link console gates output on DTR; a raw `os.open` reads
   nothing after a power-cycle/reset clears DTR. The reader must assert DTR
   (pyserial `Serial(...).dtr=True`). **This resolves a mis-attribution:** several
   "zero output" captures earlier in this session were read as a *reconnect wedge*,
   but after fixing DTR a **pin-reset reconnected cleanly** (rep 2). So this session
   does **not** independently confirm the central-power-cycle wedge — that phenomenon
   rests on prior-session evidence ([[ble5-l2cap-throughput-benchmark]]), not on this
   session's zero-captures, which were confounded with DTR.

## Implication for FSU resolution — decision: leave unresolved, ship the caveat

The CoC pump does **not** saturate (it delivers ~121 KB/s below the ~167 model with the
limiting layer unresolved), so running the frozen off/100/70/52 spacing sweep on it is
**not worth it** — an unsaturated pump can't convert freed air time into goodput.

Crucially, a **paired air-time / completion-gap / observer method CANNOT fill the
throughput or duplex bar.** The SDC +15 % / +14.2 % figures are *receiver-delivered
goodput*; air-time timing can confirm a tIFS reduction but is a different quantity and
must not be relabeled a throughput dividend. The open FSU air-time reduction is already
physically established at 1M by the [[fsu-onair-observer]] (Q3, tIFS 150→100 µs).

**Decision (customer charts): keep open+FSU throughput & duplex as UNRESOLVED**, with:
> Open FSU physically reduces tIFS on air at 1M; open-controller throughput shows
> **+5.72 % at 1M/53.75 ms**, while the **2M throughput and duplex dividends remain
> non-identifying on this rig.**

## If mechanism localization is pursued later (not a shipping prerequisite)

This preflight is bounded, not conclusive. Localizing *why* the open pump stays at ~121
would be performance research needing: pool-allocation **wait time**, `.sent`
completions / outstanding depth, credit history, controller **queue depth at event
termination**, and exact binary provenance (now archived in `firmware/`). "SDC leads at
50 ms" is descriptive across *whole rigs* (host stack, pump, scheduling, transport all
differ) — SDC connection-event-length extension is a plausible *hypothesis* for the
difference, not an established controller-only cause.

## Files

- `kforever-reps/` — the two reset-isolated K_FOREVER reps (rep 1 = 60 s; rep 2 =
  fresh post-pin-reset session, 54 s). Central logs carry the occupancy RPT lines;
  sink logs carry the cumulative-byte goodput.
- `clean-session-logs/` — the initial K_NO_WAIT session (banners + gates + goodput +
  2 occupancy windows). Kept because it contains the full gate trace.
- `app-central/`, `app-sink/` — sources + prj.conf as built. Central = K_FOREVER
  pool-64 pump (the reps' firmware). Sink = LOG=n seg_recv, 64 credits.
- `firmware/` — exact flashed HEX + ELF + .config for both boards (binary provenance).
- `capture-tool.py` — pyserial capture with DTR asserted (the reader that works).
