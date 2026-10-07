# pca10040-radio-observer — on-air FSU tIFS observer (nRF52832 DK)

A passive raw-radio timestamping sniffer that physically measures the inter-frame spacing (tIFS)
between packets of a live BLE connection, so a Frame Space Update can be confirmed **on air** rather
than taken from the controller's own report. It is the instrument behind the accepted 1M
(`debug-evidence/fsu-q3a-20260812/`) and 2M (`debug-evidence/q3-2m-accept-20260906/`) on-air results.

The BLE controller is not used: the firmware drives `RADIO`/`TIMER0`/`PPI` directly, timestamps
`RADIO.ADDRESS` and `RADIO.END` per packet at 16 MHz (62.5 ns ticks), and streams compact records.
The gap is `next.address − prev.end`, calibrated against a frozen per-PHY baseline. **nRF52 only** —
it relies on nRF52 `RADIO`/`PPI`/`END` semantics and will not build for nRF54 as-is.

## Build variants
| Use | Build |
|---|---|
| Live connection at **2M** (runtime-configured by the runner) | `west build -b nrf52dk/nrf52832 apps/nrf52/pca10040-radio-observer -- -DQ2=1 -DAIRTIME_MIN_TICKS=256` |
| Live connection at **1M** (builds; analysis needs a fresh 1M calibration, see REPRODUCE) | `west build -b nrf52dk/nrf52832 apps/nrf52/pca10040-radio-observer -- -DQ2=1` |
| Synthetic calibration pairs from the nRF54 generator (`apps/misc/z54-q1-generator`) | `-- -DQ1=1` (add `-DPHY2M=1` for 2M; build the generator with the same `-DPHY2M=1`) |

`-DAIRTIME_MIN_TICKS=256` is required at 2M: a 2M empty PDU is ~320 ticks, and the default 1M floor
(500) would drop empties and break the gap chain. In live-connection mode the PHY, access address,
CRC init and channel are sent by the runner over UART, so `-DPHY2M` is not used there.

## Running it
The quickest end-to-end check is the smoke wrapper, which detects the three boards, builds all three
images, flashes, captures and analyzes:

```
tools/observer-smoke.sh 2m
```

See REPRODUCE.md, "On-air FSU observer", for what a pass looks like, board placement, and how a
smoke differs from an accepted cell.

**Console:** on the nRF52 DK the observer prints on the J-Link's **vcom 0** port (the first of its two
`/dev/cu.usbmodem…`/`/dev/ttyACM…` ports). Output is gated on DTR, so read it with
`tools/capture-tool.py` (or pyserial with DTR asserted), not `cat`. A healthy boot ends with
`CONFIG-READY Q2`.

## Limits
- **Loss-limited:** a single antenna offset from the link. Its packet-loss rate is an upper bound, and
  it cannot tell "no reduced-spacing packet on air" from "missed it", so an on-air *negative* needs a
  positive control first.
- The **95% phase-retention gate is frozen**. A rep that falls short is re-run, not relaxed.
- This is observer-based acceptance, not qualification by a professional protocol analyzer.
