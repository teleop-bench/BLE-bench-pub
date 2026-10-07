# CoC vs GATT range test (2026-08-25) — is CoC more RF-fragile than GATT?

**Answer: No.** At matched single-segment payload, CoC and GATT lose the same ~5–7% going
10 cm → 2 ft. The larger drop seen earlier with 480 B CoC is a **segmentation** effect (2 PDUs per
SDU, both must arrive), not a CoC-vs-GATT transport difference — and it applies to any
multi-fragment transfer.

## Rig
- 2× nRF54L15-DK (PCA10156). Central SN 1057794857, peripheral/sink SN 1057719509. (A 3rd board,
  nRF52832-DK PCA10040 SN 1050347760, was connected but NOT used.)
- Open Zephyr `ll_sw_split` + `fsu-m0` (v4.4.1-15-g1772af7b563e). 2M PHY, DLE 251.
- All cells verified on air: `interval=12 → 15.00 ms`, `PHY tx=2 rx=2`, `DLE tx_max=251`.
- Throughput = receiver goodput (`SINK rx` cum_total slope for CoC; `GATT-WRITE rx` for GATT).
- Two placements set by hand: **~10 cm** and **~2 ft**. Single-run per cell; medians below drop
  the mid-capture reset-dip outliers.

## Matched-payload 2x2 (244 B, 1 segment, 15 ms, no-FSU, 2M)
| transport | 10 cm | 2 ft | retained | drop |
|---|---|---|---|---|
| CoC-244 (write via L2CAP CoC) | ~158 | ~150 | 95% | **5%** |
| GATT-244 (Write-Without-Response) | ~156 | ~145 | 93% | **7%** |

**CoC and GATT are indistinguishable** — both ~150 KB/s at 2 ft, both lose ~5–7% with distance.
CoC is not more RF/distance-sensitive than GATT.

## The real driver — segmentation (from the 480 B runs, same session)
| CoC payload @ 2 ft | throughput | drop from ~10 cm (~160) |
|---|---|---|
| **480 B (2 segments)** — FSU-on 121.6 / FSU-off 117.8 | ~120 | **~25%** |
| **244 B (1 segment)** | ~150 | ~5% |

A 480 B SDU = 2 PDUs; **both** must arrive for reassembly, so its effective loss is ~2× a
single-PDU transfer. That is why 480 B CoC fell ~25% at 2 ft while 244 B (CoC *or* GATT) held.
This is a **payload/segmentation property, avoidable** by using ≤MPS single-segment SDUs, and it
is **not CoC-specific** — a large multi-notification GATT transfer would reassemble the same way.

## Bearing on the retracted "CoC is RF-fragile" thesis
This closes it out with paired data: the transport is not the variable. The earlier apparent
CoC fragility was (1) the 50 ms-vs-15 ms measurement artifact (see `coc-technical-overview.md`
§4/§9.5), and (2) when a real distance drop did appear, it was the 2-segment 480 B payload, not
CoC. Mechanism is measured, not credit-flow speculation: **multi-segment reassembly amplifies
loss.**

## Bounds
Single-run per cell; placements hand-set (~10 cm / ~2 ft), not a calibrated attenuation axis; no
RSSI/PER captured (goodput only); 2 ft is "quieter than a stepped-attenuation sweep would probe."
The *relative* result (CoC≈GATT at matched payload; segmentation is the driver) is robust to these;
absolute KB/s are room-specific. A stepped-distance/attenuation sweep with RSSI/PER (Test 1) would
turn the 2-point drops into curves, but is not needed to answer the transport question.

Raw logs in `logs/`.

## FSU re-test @ 10 cm (ABBA, 480 B / 15 ms, verified spacing)
| run | FSU | median KB/s |
|---|---|---|
| A1 | spacing=52 | 165 |
| B1 | fsu=0 | 149 |
| B2 | fsu=0 | 152 |
| A2 | spacing=52 | 163 |

**FSU-on 164.0 vs FSU-off 150.5 -> +9.0%** (drift-cancelled). FSU converts in the packing-limited
close-range regime; it read ~0% at 2 ft only because the link was refill/distance-starved (7.8
PDU/ev, huge idle airtime -> nothing to convert). Gain is below the historical +14-23% band
because even at 10 cm the event packs only ~10 PDU/ev vs the ~35-41 cap (partially packing-bound):
FSU reclaims airtime in proportion to how airtime-bound the event is.

## Duplex FSU re-confirm @ 10 cm (GATT duplex, 25 ms, ABBA, verified spacing)
Prebuilt duplex hexes (central-fsuON/OFF-20 = 25 ms; periph-gecho). Aggregate = central `exkBps`
(B->A echo) + periph `rxkBps` (A->B blast). ON runs verified `spacing=52`; OFF none.

| run | B->A | A->B | aggregate |
|---|---|---|---|
| ON a  | 104 | 103 | 207 |
| OFF a | 93  | 93  | 186 |
| OFF b | 92  | 92  | 184 |
| ON b  | 103 | 102 | 205 |

**FSU-ON 206 vs FSU-OFF 185 -> +11.4%.** Reproduces the historical 2 ft ABBA n=4 result almost
exactly (off 159.8 -> on 178.8, +11.9%): the **delta transfers** across distance even though the
absolutes are higher at 10 cm (fuller events). Duplex is near-symmetric (~103 KB/s each way).
Confirms duplex FSU is real and the earlier finding holds at a controlled, verified operating point.

## One-way FSU vs interval @ 10 cm (480 B, ABBA, FRESH RESET per run, tags verified)
Tags verified fresh (ON `fsu=52`, OFF `fsu=0`) — the first attempt without per-run reset gave
STALE `fsu=52` on OFF runs and unreliable numbers; **reset both boards fresh before every capture.**

| interval | ON | OFF | gain |
|---|---|---|---|
| 15 ms | 164.0 | 150.5 | **+9.0%** |
| 25 ms | 156.5 | 161.0 | -2.8% (noisy) |
| 37.5 ms | 163.2 | 162.5 | +0.5% |
| 50 ms | 158.5 | 154.8 | +2.4% |

**One-way FSU converts ~+9% at 15 ms, ~0-2% at 25-50 ms — contradicting the historical +14.4%
@50 ms.** Reconciliation: the open link is **refill-bound (~10 PDU/event) far below the tIFS wall
(~35-41)**, and FSU only converts when tIFS is the binding constraint (event packed to the wall).
Refill keeps the event well short of the wall, so FSU has little to convert — partial at 15 ms
(tightest window), ~0 at longer intervals. **FSU's payoff on this open stack is GATED by the
refill limit**; raise the event toward the ~35 wall (deeper feed/buffers) and FSU converts, leave
it refill-bound and FSU is mostly moot. Historical +14-23% = a fuller-packing condition. Do not
read a single FSU % as fixed. (Duplex differs: the 2nd stream adds packing pressure FSU relieves —
duplex +11.4% @25 ms held, see above.)

## CoC-duplex stall re-check @ 10 cm — STALL GONE on current tree (v4.4.1-15-g1772af7)
Rebuilt the committed duplex apps (`coc-duplex-artifact-20260814/firmware/cocdx-{cen,sink}-main.c`
+ `dx-sink.conf` with `AUTO_DATA_LEN_UPDATE=y` — the exact 08-14 stall trigger). DLE confirmed on
air: peripheral **`tx_max=251`** (the trigger). Result: **uplink FLOWS** — central `CENRX` climbs,
sink `UP sent` climbs 930→5809, `txcred=0` (normal) — vs the 08-14 hard stall (uplink=0, `UP sent`
frozen 64, `txcred` frozen 64).

| metric | 08-14 (bug) | now |
|---|---|---|
| peripheral TX DLE | 251 | 251 |
| uplink B->A | 0 (hard stall) | **60.4 KB/s** |
| downlink A->B | ~90-100 alone | 120.7 KB/s |
| **duplex aggregate** | **~90-100** | **181.1 KB/s** |

**The peripheral L2CAP-TX-DLE-251 stall does NOT reproduce.** CoC-duplex now works, ~181 KB/s
aggregate (asymmetric: downlink 120 > uplink 60, the downlink blast still partly starves uplink;
`lasterr=-128` seen but data flows). Root-cause of the fix not bisected — stated empirically.
Overturns the "CoC-duplex bug-limited ~90-100" claim (coc-technical-overview.md §5 / memory).

## CoC-duplex stall root-cause investigation — both hypotheses FALSIFIED
Set out to root-cause why the 08-14 stall doesn't reproduce. Tested two concrete candidates:
1. **08-14 "host l2cap.c DLE=251 bug":** `git log v4.4.1..HEAD -- subsys/bluetooth/host/l2cap.c
   conn.c` on the fsu-m0 branch = **empty**. The host is byte-identical to stock v4.4.1, and 08-14
   ran the same base -> "host bug fixed since" is impossible. FALSIFIED.
2. **FSU event-fill ISR diagnostic starves peripheral TX:** rebuilt both duplex apps with
   `-DCONFIG_BT_CTLR_FSU_EVENTFILL_DIAG=y` (verified `=y` in `.config`), reproducing the 08-14
   always-on state. Uplink still FLOWS (identical UP sent 930->5809, CENRX climbing). FALSIFIED.

Conclusion: **no reproducible CoC-duplex bug on the current tree; the 08-14 stall's cause is
unrecoverable because the 08-14 build was under-provenanced** (only main.c + partial dx-sink.conf
archived — not the full prj.conf / tree commit / rig). The 08-14 host-l2cap.c diagnosis is
contradicted by re-test. PITFALL: a confident root-cause on an unarchived build did not survive
re-test (same failure mode as the "RF-collapse" artifact) -> archive the FULL build state.

## Refill limit — buffer depth is NOT the lever (2026-08-25)
Rebuilt central with 96 TX buffers (ACL_TX_COUNT=96, L2CAP_TX_BUF_COUNT=96, EVT_RX_COUNT=100) +
sink CTLR_RX_BUFFERS=16 (vs stock 64/8). @10cm/15ms/480B: **160 KB/s, 10.2 PDU/event — identical
to stock (162, 10.4).** So the ~10 PDU/event refill wall is **per-event refill-rate/scheduling,
not buffer depth**; the event ends after ~10 PDUs (~1.5ms) in a 15ms interval despite deep buffers
+ continuous data. Historical "deep buffers 132->142" was @50ms (long event drains deeper queue) —
depth helps only at long intervals. Root-cause needs LLL instrumentation (host->controller
per-event staging cadence); deferred high-effort. NOT credits (eagain=0), NOT app-pool (poolfail=0),
NOT host buffers (deep), NOT buffer depth (tested).
