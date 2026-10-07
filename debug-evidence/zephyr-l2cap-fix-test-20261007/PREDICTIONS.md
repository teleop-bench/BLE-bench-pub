## Patch test (pre-registered 2026-10-07, before running)
Suggested host fix (l2cap-fix.patch: l2cap_chan_tx_give_credits() returns early while credits are 0, and re-raises the
channel whenever credits arrive with data queued) built into the ACCEPTOR only; initiator = the default (credits after
connect, the build that stalled 6/6). Same-session control: unpatched acceptor. Order patched, unpatched, unpatched,
patched (3 connections per capture). Prediction: patched 6/6 healthy (done tracks sent), unpatched 6/6 stalled.
Falsified if the patched acceptor still stalls.
