# CoC-duplex "≈ one-way" was an artifact — RETRACTION + true measurement (2026-08-14)

## Verdict
The systematic-sweep **CoC-duplex facet (~168 KB/s ≈ one-way) is invalid** (downlink-only;
the reverse stream was wedged). The honest achievable number on this open stack is
**~90–100 KB/s aggregate**, and the cause is now root-caused: **a peripheral L2CAP-CoC-TX
stall that triggers when the peripheral's data length is extended to 251** (see ROOT CAUSE
below). It is a firmware/host-stack bug, **not** CoC's protocol ceiling and **not** an
inherent credit tax. Symmetric full-rate CoC-duplex is unreachable on this stack without a
fix. Vindicates the original ~103 smoke test.

## Proof the original sweep was corrupted
`cx-*-cen-CENRX-tail.txt`: the duplex central's uplink receive counter
`CENRX cum_total=0` from first line to last across the whole run — the central never
received a single uplink byte. The number charted as "duplex aggregate" was the sink's
downlink only.

## Root cause
Downlink blast thread starves the BT RX that returns L2CAP credits → the uplink sender
runs out of credits and stalls. Intermittent in the original batch build (uplink died
3/4 runs); once dead, downlink alone reads at the one-way rate (~168), which was silently
logged as "aggregate".

## ROOT CAUSE (option-b re-engineering, decisive) — `DLE-stall-evidence.txt`, `firmware/`
The ~90–100 cap is **not** CoC's credit tax. It is a **peripheral L2CAP-CoC-TX stall that
triggers when the peripheral's data length (DLE) is extended to 251**.

Debug chain (DLE-instrumented sink: `up_sent / up_eagain / lasterr / txcred` in the SINK rx line):
1. Central logged `DLE: tx_max=251 rx_max=27` — the central's RX pinned at 27 because the
   peripheral never extended its own TX data length → uplink fragmented to 27-octet PDUs →
   fragmentation overhead dragged **both** directions to ~45 each (~90 agg).
2. Extending the peripheral DLE to 251 — via **either** an app `bt_conn_le_data_len_update()`
   in the connected callback **or** `CONFIG_BT_AUTO_DATA_LEN_UPDATE=y` — **hard-stalls** the
   peripheral's L2CAP TX: `bt_l2cap_chan_send` returns 0, queues exactly `pool` SDUs, then
   blocks forever on `net_buf_alloc(K_FOREVER)`. `up_sent` freezes at pool size, `eagain=0`,
   `lasterr=0`, and **`tx.credits` never decrements** — the host accepted the SDUs but never
   handed a single K-frame segment to the controller. Central `CENRX` stays 0.
3. **Controlled proof it's the DLE extension:** DLE-stays-27 → uplink flows (~45); any
   extension to 251 → stall. Reproduced 3× (app-call pool16, app-call pool64, AUTO). Stalls
   even **uplink-only** (central downlink disabled via `-DAPP_NO_DOWNLINK`) → not contention.
4. **Not a config mismatch:** sink and central `.config` are identical for
   `BT_BUF_ACL_TX_SIZE=251` / `CTLR_DATA_LENGTH_MAX=251` / `PHY_2M`, yet the **central**
   transmits 251-octet PDUs fine (downlink 140–170) and the **peripheral** stalls →
   **peripheral-role firmware/host-stack bug, not a knob set wrong.**

## Red herrings ruled out
TX-queue depth (pool 64→16 changed nothing), credit-return watermark, `k_yield` pacing —
all irrelevant. The single causal variable is peripheral DLE=27 vs 251.

## Consequence for the benchmark
Symmetric full-size CoC-duplex is **not achievable on this open stack** without fixing the
peripheral-TX-DLE stall. Honest achievable number: **~90–100 KB/s aggregate**, uplink
fragment-throttled. Matrix's 4th cell charted as an achievable band + one measured point
(15 ms, n=2), not a swept curve. Contrast GATT-duplex (+8%): the echo notify rides free in
the write's empty-ACK slot; CoC has no comparable free reverse path here.

## Open (deferred: high-effort / low customer value)
Root-cause the host TX-pull stall — instrument `subsys/bluetooth/host/l2cap.c`
`l2cap_data_pull` / `raise_data_ready` and the conn `tx_processor` on the peripheral to see
why the queued segment is never pulled when DLE=251. May be an upstream Zephyr bug.

## Files
`credit-batch-v2-watermark.csv` (early watermark run), `DLE-stall-evidence.txt` (the decisive
27-flows vs 251-stalls capture), `firmware/` (cocdx-cen/sink `main.c` + `dx-sink.conf`),
`cx-*-CENRX-tail.txt` (original-sweep uplink=0 proof).
