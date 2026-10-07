# §9.2 OBJECTIVE MEASUREMENT: does open-Zephyr FSU convert to throughput at 2M? — YES, +20%

**2026-08-13, nRF54L15 pair (open Zephyr controller, BT_LL_SW_SPLIT), 2M PHY, 50 ms interval,
2-channel-pinned, GATT write-without-response 244 B bulk saturating the link.**

## Result
Receiver-delivered goodput (peripheral's bulk-sink cumulative byte counter `blk=`, steady-window
least-squares slope — the SAME method both arms, `goodput_method.py`):

| pos | arm  | tIFS on-air (observer) | FSU on peer | goodput KB/s |
|-----|------|------------------------|-------------|--------------|
|  1  | f150 | 150 µs (gap 174, 1912) | no reduction (req [150..150]) | 145.0 |
|  2  | f52  | 52 µs  (gap 76, 1973)  | Q3FSU-DONE role=P spacing=52 phys=0x2 | 169.5 |
|  3  | f52  | 52 µs  (gap 76, 1931)  | Q3FSU-DONE role=P spacing=52 phys=0x2 | 159.1 |
|  4  | f150 | 150 µs (gap 174, 1933) | no reduction | 128.5 |

- **FSU-on mean 164.3 vs FSU-off mean 136.8 KB/s → +27.5 KB/s = +20.1%.**
- **Counterbalanced ABBA** (A={1,4}, B={2,3}, both centered at position 2.5) → cancels linear drift.
  Both arms drift DOWN over the session (145→128, 169→159 KB/s); ABBA removes it; effect survives.
- **Strict non-overlap: min(FSU-on)=159.1 > max(FSU-off)=145.0** across all 4 runs.
- **0 disconnects** in all 4; every rep's tIFS independently confirmed on air by the observer AND
  on the peer by the responder-side FSU-completion (`Q3FSU-DONE role=P`).

## Why this differs from the earlier "no 2M gain"
The earlier goodput rig saw no FSU effect because its events were NOT saturated (~29/35 LL
transactions/event) — freed airtime had nothing to fill. Here the 244 B blast + ATT MTU exchange
+ DLE saturate events (~1:1 DATA:empty), so the shorter tIFS (98 µs/pair saved) CONVERTS to
delivered bytes. The regime is the whole story: FSU pays off ONLY when the controller is
airtime-bound with a full event queue. This is the customer-relevant regime (bulk saturating).

## Controls present
- The f150 arm is the in-band control: the SAME auto-FSU code path runs but requests [150..150]
  (no reduction), so the only variable is the granted spacing. Observer confirms 150 vs 52 µs.
- Reset-isolated: each rep = fresh flash + fresh connection.
- Receiver-delivered metric (sink cumulative bytes), NOT sender-completion.

## Honest bounds
- n=2 per arm (one ABBA block). Strong (non-overlap + drift-cancelled) but not a large sample;
  a 2nd/3rd ABBA block would tighten the CI.
- This is the "vs Zephyr (no FSU)" leg of the objective — DEMONSTRATED +20%. The "vs proprietary
  controller+FSU" leg is a SEPARATE comparison (SoftDevice FSU support is a different question).
- §5.5 explicit clamp-removed falsifiability control not separately run; the f150 arm + the
  observer's direct 150-vs-52 measurement already serve as the physical control.
