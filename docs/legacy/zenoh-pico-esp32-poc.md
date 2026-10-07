# BLE Heartbeat Fallback PoC (legacy / origins)

> **This is the repo's *original* proof-of-concept**, kept for provenance. The repo has since
> become a Bluetooth LE 5/6 throughput & latency benchmark — see the top-level
> [README](../../README.md) and [REPRODUCE.md](../../REPRODUCE.md). Paths below were updated for
> the current layout (`apps/esp32/`, `tools/`).

Proof-of-concept for a BLE heartbeat channel that survives Wi-Fi failure.

## Architecture

```
Lemur (robot)              ESP32                    Mac (operator)
zenoh-pico pub --UART--> NUS BLE firmware ~~BLE~~> ble-serial --PTY--> zenoh sub
  heartbeat               (dumb bridge)              (Python)          heartbeat
  10 Hz, ~20B             UART<->GATT NUS           /tmp/ttyBLE        serial link
```

The ESP32 is a transparent UART-to-BLE bridge. It does NOT run zenoh.
zenoh-pico's serial transport (COBS framing + CRC32) runs end-to-end
between the Lemur publisher and the Mac subscriber, tunneled through BLE.

## Hardware Requirements

- **ESP32 dev board** (ESP32-WROOM-32 or ESP32-C3 -- any with BLE 4.2+)
- **USB-UART cable** or direct wiring between Lemur and ESP32 UART2
  - ESP32 GPIO16 (RX2) <-- Lemur TX
  - ESP32 GPIO17 (TX2) --> Lemur RX
  - GND <--> GND
  - Level shifter if voltage mismatch (ESP32 is 3.3V)
- **Lemur laptop** (or any Linux box with a serial port / USB-UART adapter)
- **Mac** with Bluetooth (operator workstation)

## Software Prerequisites

### Mac (operator side)

```bash
pip install ble-serial
python3 -m ble_serial --help
ble-scan  # should list nearby BLE devices
```

### Lemur (robot side)

```bash
# zenoh-pico with serial link enabled (external dependency)
git clone https://github.com/eclipse-zenoh/zenoh-pico.git
cmake -S zenoh-pico -B zenoh-pico/build -DZ_FEATURE_LINK_SERIAL=1
cmake --build zenoh-pico/build -j$(nproc)

# heartbeat publisher (now lives in tools/; expects zenoh-pico as a sibling checkout)
cmake -S tools -B tools/build
cmake --build tools/build
```

### ESP32 firmware

```bash
pip install platformio
cd apps/esp32/esp32-nus-bridge
pio run                     # build
pio run --target upload     # flash
pio device monitor --baud 115200
```

## Test Procedure

See [`tools/test-ble-heartbeat.sh`](../../tools/test-ble-heartbeat.sh) for the full step-by-step
procedure. Summary:

1. Flash ESP32 with NUS bridge firmware.
2. Wire ESP32 UART2 to the Lemur serial port.
3. On Mac: `python3 -m ble_serial -d <ESP32_ADDR> -p /tmp/ttyBLE`
4. On Mac: `./z_sub -k heartbeat/ble -e "serial//tmp/ttyBLE#baudrate=115200" -m peer`
5. On Lemur: `./heartbeat_pub -e "serial//dev/ttyUSB0#baudrate=115200" -m peer`
6. Verify the heartbeat arrives at 10 Hz.
7. Kill Wi-Fi on the Lemur; verify the BLE heartbeat continues.

## Serial Locator Format

```
serial/<device_path>#baudrate=<rate>
```
For absolute paths (leading slash), the double-slash is intentional:
```
serial//dev/ttyUSB0#baudrate=115200
       ^--- protocol separator + absolute path
```

## Protocol Details

- zenoh-pico serial uses **COBS framing** (Consistent Overhead Byte Stuffing).
- Each frame includes a **CRC32 checksum**.
- **MTU** 1500 bytes, **MFS** 1510 bytes.
- Serial handshake: INIT flag sent, INIT+ACK expected back.
- The ESP32 bridge is fully transparent -- it does not parse or modify the COBS frames.

## BLE Throughput (BLE 4.2 NUS, the PoC baseline)

- BLE 4.2 NUS typical throughput: 2-6 KB/s.
- Heartbeat payload: ~20 bytes + COBS overhead ≈ 30 bytes/frame; at 10 Hz ≈ 300 B/s — well within NUS.
- Round-trip latency: ~15-60 ms (depends on the BLE connection interval).

*(The main benchmark measures L2CAP CoC / GATT at ~150-158 KB/s — two orders of magnitude above
this NUS baseline — on nRF52/nRF54L15. See the top-level README.)*
