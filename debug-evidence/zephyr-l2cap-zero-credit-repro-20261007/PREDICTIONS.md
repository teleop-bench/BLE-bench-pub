# Pre-registered (2026-10-07, before the run): the shelved minimal repro = the Zephyr 0-initial-credit stall?
Pair: apps/z44/z44-repro-acceptor (peripheral; stock l2cap_coc_acceptor + a 20 ms send loop that starts on connect) and
apps/z44/z44-repro-initiator (central; stock l2cap_coc_initiator, receiver, grants its 20-credit window in the connected
callback = AFTER connect, self-reboots 25 s after each connect). Built for nrf54l15dk, open v4.4.2-16.
Control: the same initiator with -DCREDITS_IN_REQUEST=1 (window granted before bt_l2cap_chan_connect, i.e. in the
connection request; the only difference).
Prediction (Zephyr host bug: acceptor opens with 0 TX credits, its first send lowers the channel, later credits never
re-raise it): default -> the acceptor wedges on (nearly) every connection ("sent" grows, "done" frozen); control -> no
wedges, "done" tracks "sent". Falsified if default connections mostly run healthy or the control also wedges.

## Void first attempt (kept in void/caps-mtu20)
All four captures aborted at channel setup: the v4.4.2 initiator rejects the acceptor's response ("Invalid conn rsp
params: mtu 20 mps 22", spec minimum 23) and disconnects; re-advertising then failed (-ENOMEM, restarted before the
connection was freed). No bug test happened. Acceptor fixed (rx.mtu 23, re-advertise on .recycled); same predictions.
