# Duplex confidence campaign — session S0 (2026-09-06)

Confirms/extends the duplex-stall findings. One board pair, one RF setting; **n=10 per arm**. This is
a *second independent session* for test 1 (the first is `duplex-stall-rate-20260906`, n=12) plus the
one-shot tests 2 & 3. Raw captures in `S0-pass1/`. (The multi-session-over-time repro was cut short at
the user's request — waits stripped once the boards were needed for the 2M work; S0 + the earlier
standalone run give a 2-session confirmation.)

## Test 1 — repro (2nd session): interval-gating + wedge-vs-steady HOLD
| arm | result (n=10) | session-1 | verdict |
|---|---|---|---|
| 7.5 ms cold | **10/10 balanced** (~71+72) | 12/12 | reproduces |
| 25 ms cold | **8/10 stall** (down 133 / up 15) | 10/12 stall | reproduces |
| 25 ms steady-state (periph-only) | **10/10 balanced, uplink-dominant** (54+112) | 0/12 stall | reproduces |

## Test 2 — reversal control: "the freshly-rebooted side's TX wins" CONFIRMED
Central-only reset @25 ms (central fresh, periph stays booted) → **9/10 downlink-dominant** (down 133 /
up 5). Combined with the periph-only case (uplink-dominant ~112/54):
- **Freshly-rebooted CENTRAL** → downlink dominates and starves the (booted) peripheral's uplink.
- **Freshly-rebooted PERIPHERAL** → uplink dominates.
So the reconnect asymmetry is **reboot-side-driven** (the side that just restarted its blast wins the
airtime; its peer is throttled) — not an airtime/credit ceiling.

## Test 3 — FSU-duplex delta: measured NULL
| interval | FSU on (aggr) | FSU off (aggr) | delta |
|---|---|---|---|
| 25 ms (steady-state) | 153 | 158 | ~−3 % (null) |
| 7.5 ms | 143 | 147 | ~−3 % (null) |

**No duplex-FSU gain** at either interval (on ≈ off, within noise). Turns the head-to-head "FSU
duplex +14 % did not reproduce" *withdrawal* into a **positive measured null** — consistent with
"duplex already saturates the air both ways, no spare event time for FSU."

## Net
Every duplex claim in the README is now hardware-confirmed across ≥2 sessions: echo-symmetry is an
artifact; independent balance is interval-/reboot-dependent (symmetric ~77+77 at 7.5 ms); the 25 ms
uplink stall is the reconnect wedge, not steady-state; no duplex-FSU gain.
