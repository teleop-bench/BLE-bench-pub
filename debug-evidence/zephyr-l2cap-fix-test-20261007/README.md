# Suggested Zephyr host fix for the 0-initial-credit L2CAP stall: tested (2026-10-07)

**Status: RESOLVED (pre-registered; `PREDICTIONS.md`).** Tests the patch proposed in the upstream issue draft for the
stall reproduced in `zephyr-l2cap-zero-credit-repro-20261007`.

**Patch** (`l2cap-fix.patch`, against `subsys/bluetooth/host/l2cap.c` of v4.4.2-16; the function is identical in
upstream `main` @ 653d367): `l2cap_chan_tx_give_credits()` returns early while the channel has 0 credits (so a channel
accepted with 0 initial credits is not marked sendable), and re-raises the channel whenever credits are added and data
is queued, outside the `STATUS_OUT` transition. Applied to the fork's working tree only to build the acceptor, then
reverted (the fork stays pristine).

**Setup:** the repro pair of `zephyr-l2cap-zero-credit-repro-20261007`; initiator = the default build (credits granted
after connect, which stalled the unpatched acceptor 6/6); acceptor patched vs unpatched, order patched, unpatched,
unpatched, patched (`run_patch.py`).

## Result (`run-patch.log`, `caps/`)
| acceptor | connections to a freshly booted central | stalled (0 completions) | healthy |
|---|---|---|---|
| **patched** | 9 | **0** | **9** (816–1,238 SDUs completed per connection, ~50/s) |
| unpatched (same session) | 6 | **6** (`sent=4 done=0` for the whole connection) | 0 |

The first entry of captures r2, r3 and r4 is excluded in both arms: the acceptor (reset first) connected to the previous
capture's still-running central, which was then reset ~0.4 s into that connection (the central log reboots at 2.17 s;
the acceptor's link ends at ~5.8 s with reason 0x08, supervision timeout), so those entries measure a vanished peer, not
the bug. Including them would add one "stalled" entry to the patched arm and one partial one to the unpatched arm.

**Reading:** with the patch, an acceptor whose channel opens with 0 TX credits resumes transmitting as soon as the
initiator's credits arrive; without it, the same sequence stalls every time. The fix is suitable to propose upstream.

Files: `PREDICTIONS.md`, `l2cap-fix.patch`, `run_patch.py`, `run-patch.log`, `caps/`, `firmware/` (+ SHA256SUMS: patched
acceptor, unpatched acceptor, default initiator), `configs/`.
