#!/usr/bin/env bash
# End-to-end BLE heartbeat fallback test procedure
#
# Architecture:
#   Lemur (robot)              ESP32                    Mac (operator)
#   zenoh-pico pub --UART--> NUS BLE firmware ~~BLE~~> ble-serial --PTY--> zenoh sub
#     heartbeat                (dumb bridge)              (Python)         heartbeat
#
# Prerequisites:
#   - ESP32 flashed with esp32-nus-bridge firmware
#   - ESP32 UART2 wired to Lemur serial port (or USB-UART adapter)
#   - ble-serial installed on Mac: pip install ble-serial
#   - zenoh-pico built with Z_FEATURE_LINK_SERIAL=1 on Lemur
#   - zenoh router running (or use peer mode)

set -euo pipefail

ESP32_BLE_ADDR="${ESP32_BLE_ADDR:-}"
SERIAL_DEV="${SERIAL_DEV:-/dev/ttyUSB0}"
BAUD_RATE="${BAUD_RATE:-115200}"
BLE_PTY="${BLE_PTY:-/tmp/ttyBLE}"
KEYEXPR="${KEYEXPR:-heartbeat/ble}"

echo "=== BLE Heartbeat Fallback Test ==="
echo ""

# --- Step 0: Scan for ESP32 if address not set ---
if [ -z "$ESP32_BLE_ADDR" ]; then
    echo "Step 0: Scanning for ESP32 BLE device..."
    echo "  Run: ble-scan"
    echo "  Look for device named 'zenoh-ble-bridge'"
    echo "  Then set: export ESP32_BLE_ADDR=<address>"
    echo ""
    echo "  Example:"
    echo "    $ ble-scan"
    echo "    ...found zenoh-ble-bridge (AA:BB:CC:DD:EE:FF)..."
    echo "    $ export ESP32_BLE_ADDR=AA:BB:CC:DD:EE:FF"
    echo ""
    exit 1
fi

# --- Step 1: Flash ESP32 ---
echo "Step 1: Flash ESP32 (skip if already flashed)"
echo "  cd esp32-nus-bridge && pio run --target upload"
echo ""

# --- Step 2: Wire ESP32 UART to Lemur ---
echo "Step 2: Verify wiring"
echo "  ESP32 GPIO16 (RX2) <-- Lemur TX"
echo "  ESP32 GPIO17 (TX2) --> Lemur RX"
echo "  ESP32 GND          --- Lemur GND"
echo "  (Use level shifter if Lemur is 5V logic)"
echo ""

# --- Step 3: Start ble-serial on Mac ---
echo "Step 3: Starting ble-serial..."
echo "  Connecting to ESP32 at $ESP32_BLE_ADDR"
echo "  Virtual PTY will be at $BLE_PTY"
echo ""

NUS_SERVICE="6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_WRITE="6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_READ="6e400003-b5a3-f393-e0a9-e50e24dcca9e"

echo "  Command:"
echo "    python3 -m ble_serial -d $ESP32_BLE_ADDR \\"
echo "      -s $NUS_SERVICE \\"
echo "      -w $NUS_WRITE \\"
echo "      -r $NUS_READ \\"
echo "      -p $BLE_PTY"
echo ""
echo "  Run this in a separate terminal on the Mac."
echo "  Wait for 'Connected' message before proceeding."
echo ""

# --- Step 4: Start zenoh subscriber on Mac ---
echo "Step 4: Start zenoh subscriber on Mac"
echo "  The subscriber reads from the ble-serial virtual PTY."
echo ""
echo "  Use zenoh-pico z_sub example:"
echo "    cd ../zenoh-pico/build"
echo "    ./z_sub -k '$KEYEXPR' -e 'serial/$BLE_PTY#baudrate=$BAUD_RATE' -m peer"
echo ""
echo "  NOTE: zenoh-pico serial transport uses COBS framing with CRC32."
echo "  Both endpoints must use zenoh-pico serial -- raw serial will NOT work."
echo ""

# --- Step 5: Start heartbeat publisher on Lemur ---
echo "Step 5: Start heartbeat publisher on Lemur"
echo "  SSH to Lemur and run:"
echo ""
echo "    ./heartbeat_pub -e 'serial/$SERIAL_DEV#baudrate=$BAUD_RATE' -m peer"
echo ""
echo "  Expected output:"
echo "    === BLE Heartbeat Publisher ==="
echo "    Key expression: heartbeat/ble"
echo "    Publishing heartbeat at 10 Hz. Press CTRL-C to quit."
echo "    [0] heartbeat ts=123456789"
echo "    [1] heartbeat ts=123456889"
echo ""

# --- Step 6: Verify heartbeat arrives ---
echo "Step 6: Verify heartbeat"
echo "  On the Mac subscriber terminal, you should see:"
echo "    >> Received ('heartbeat/ble': 'HB:0:123456789')"
echo "    >> Received ('heartbeat/ble': 'HB:1:123456889')"
echo ""
echo "  Verify: messages arrive at ~10 Hz (100ms interval)"
echo ""

# --- Step 7: Kill WiFi, verify BLE survives ---
echo "Step 7: Kill WiFi on Lemur, verify BLE heartbeat continues"
echo "  On Lemur:"
echo "    sudo ip link set wlp0s20f3 down"
echo ""
echo "  Verify: heartbeat messages CONTINUE arriving on Mac subscriber."
echo "  The BLE path is independent of WiFi."
echo ""
echo "  Restore WiFi:"
echo "    sudo ip link set wlp0s20f3 up"
echo ""

# --- Troubleshooting ---
echo "=== Troubleshooting ==="
echo ""
echo "Problem: ble-serial cannot find ESP32"
echo "  - Run 'ble-scan' and verify 'zenoh-ble-bridge' appears"
echo "  - Check ESP32 serial monitor for 'BLE advertising started'"
echo "  - Ensure ESP32 is not already connected to another device"
echo "  - On macOS, Bluetooth must be ON in System Settings"
echo ""
echo "Problem: zenoh session fails to open over serial"
echo "  - Check baud rate matches (default 115200) on both ends"
echo "  - Verify serial device path (/dev/ttyUSB0 on Linux, /tmp/ttyBLE on Mac)"
echo "  - Check wiring: TX->RX, RX->TX (crossover)"
echo "  - Monitor ESP32 USB console for debug output"
echo "  - zenoh-pico serial does a COBS INIT handshake -- if it hangs,"
echo "    the bridge may not be forwarding bytes correctly"
echo ""
echo "Problem: heartbeat arrives but with high latency"
echo "  - BLE NUS has ~7.5-30ms connection interval overhead"
echo "  - Check BLE_CHUNK_SIZE in ESP32 firmware"
echo "  - Monitor ESP32 MTU negotiation in serial console"
echo ""
echo "Problem: heartbeat drops or corrupted data"
echo "  - zenoh-pico COBS framing includes CRC32 -- corrupted frames are dropped"
echo "  - BLE GATT notifications are unreliable (no ACK at GATT layer)"
echo "  - For production: use write-with-response or L2CAP CoC (Path 3)"
echo ""
echo "Expected throughput:"
echo "  - BLE 4.2 NUS: ~2-6 KB/s typical (depends on connection interval)"
echo "  - Heartbeat payload: ~20 bytes + COBS overhead (~30 bytes total)"
echo "  - At 10 Hz: ~300 bytes/sec -- well within BLE NUS capacity"
