# Minimal reproduction of the Zephyr L2CAP 0-initial-credit TX stall (2026-10-07)

**Status: RESOLVED (deterministic repro + causal control, pre-registered).** The upstream-ready reproduction of the
host bug identified in `coc-credit-fixes-20261006`. The pair is Zephyr's stock `l2cap_coc_acceptor` /
`l2cap_coc_initiator` samples with minimal additions (`apps/z44/z44-repro-*`), built for nRF54L15-DK on open
`v4.4.2-16-gfee9fbc` (host code identical to upstream v4.4.2; the fork adds only controller patches and a
`CONFIG_BT_TESTING`-gated counter in `l2cap.c`).

## Setup
- Acceptor (peripheral): accepts the channel and starts sending 20-byte SDUs every 20 ms the moment it connects;
  counts submitted (`sent`) vs completed (`done`, the `.sent` callback); 4-buffer pool.
- Initiator (central, receiver, `seg_recv`): grants its 20-credit window in the channel-connected callback, i.e.
  **after** connect, so the acceptor's channel opens with **0 TX credits**; self-reboots 25 s after each connect, so
  each 70 s capture holds 3 fresh connections.
- Control: the same initiator built with `-DCREDITS_IN_REQUEST=1`, which grants the window before
  `bt_l2cap_chan_connect` (it rides in the connection request). Default build unchanged.
- Order default, control, control, default; both boards reset per capture (`run_repro.py`).

## Result (`summary.txt`, `caps/`)
| build | connections | stalled (0 completions while the channel is up) | healthy |
|---|---|---|---|
| default (credits after connect) | 6 | **6** (each: `sent=4 done=0` for the whole ~25 s, allocation failures climbing) | 0 |
| control (credits in the request) | 6 | 0 | **6** (~1,230 SDUs completed per 25 s) |

The prediction held. Mechanism (`subsys/bluetooth/host/l2cap.c`, unchanged in `main` as of 2026-10-06): on accept the
host calls `l2cap_chan_tx_give_credits(le_chan, 0)`, which sets `BT_L2CAP_STATUS_OUT` with 0 credits; the first send
finds no credits and lowers the channel from the TX ready list without clearing `STATUS_OUT`; when the credits arrive,
`l2cap_chan_tx_give_credits()` re-raises the channel only if `STATUS_OUT` was clear, so it never does. The acceptor's
`bt_l2cap_chan_send()` keeps returning success while nothing is transmitted.

## Notes
- `void/caps-mtu20/`: the first attempt, void. The original acceptor set `rx.mtu = 20`, below the spec minimum (23);
  v4.4.2 rejects that ("Invalid conn rsp params: mtu 20 mps 22") and disconnected at once, and re-advertising in the
  disconnected callback failed (-ENOMEM). Fixed: `rx.mtu = 23`, re-advertise on `.recycled`. No bug test happened in
  the void captures.
- The 2026-08/09 write-ups that blamed a central reboot (`apps/nrf52/nrf52-l2cap-echo/zephyr-bug-report.md`,
  `uplink-coc-diag-20260812`) were seeing this bug: their minimal pair "stalled before any reboot" because its initiator
  grants credits only after connect.

Files: `PREDICTIONS.md`, `run_repro.py`, `summary.txt`, `caps/`, `void/`, `firmware/` (+ SHA256SUMS: acceptor,
initiator default, initiator control), `configs/`.
