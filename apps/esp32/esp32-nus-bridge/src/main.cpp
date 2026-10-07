// ESP32 NUS (Nordic UART Service) BLE Bridge
//
// Transparent bidirectional bridge: UART <-> BLE NUS
// The ESP32 does NOT run zenoh -- it just forwards raw bytes.
// zenoh-pico runs on the Linux host and uses COBS-framed serial protocol.
//
// Wiring (ESP32 to Linux host):
//   ESP32 GPIO16 (RX2) <-- Linux TX (via level shifter if needed)
//   ESP32 GPIO17 (TX2) --> Linux RX
//   ESP32 GND          --- Linux GND
//
// On the Mac side, ble-serial creates a virtual PTY from this BLE device.

#include <Arduino.h>
#include <NimBLEDevice.h>

// ----- Configuration -----
#define DEVICE_NAME       "zenoh-ble-bridge"
#define UART_BAUD_RATE    115200

// UART2 pins (ESP32 default). NOT UART0 which is USB debug console.
#define UART_RX_PIN       16
#define UART_TX_PIN       17

// LED for connection status
#define LED_PIN           2   // Built-in LED on most ESP32 dev boards

// BLE NUS (Nordic UART Service) standard UUIDs
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (host -> ESP32)
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify (ESP32 -> host)

// BLE MTU overhead: 3 bytes ATT header
// With default MTU 23: max payload = 20 bytes
// With negotiated MTU 512: max payload = 509 bytes
// zenoh-pico COBS frames can be up to ~1516 bytes, so we chunk in BLE TX.
#define BLE_CHUNK_SIZE    240  // Conservative chunk size for BLE notify

// UART read buffer
#define UART_BUF_SIZE     2048

// ----- Globals -----
static NimBLEServer *pServer = nullptr;
static NimBLECharacteristic *pTxCharacteristic = nullptr;
static bool deviceConnected = false;
static bool oldDeviceConnected = false;
static uint8_t uartBuf[UART_BUF_SIZE];

// ----- LED patterns -----
static unsigned long lastLedToggle = 0;
static bool ledState = false;

void updateLed() {
    if (deviceConnected) {
        // Solid on when connected
        digitalWrite(LED_PIN, HIGH);
    } else {
        // Slow blink (500ms) when waiting for connection
        unsigned long now = millis();
        if (now - lastLedToggle >= 500) {
            lastLedToggle = now;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        }
    }
}

// ----- BLE Callbacks -----
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
        (void)pServer;
        Serial.print("BLE connected: ");
        Serial.println(connInfo.getAddress().toString().c_str());
        deviceConnected = true;

        // Request higher MTU for better throughput
        // (zenoh-pico COBS frames can be up to 1516 bytes)
        pServer->updateConnParams(connInfo.getConnHandle(), 6, 12, 0, 400);
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
        (void)pServer;
        (void)connInfo;
        Serial.printf("BLE disconnected, reason=%d\n", reason);
        deviceConnected = false;
    }

    void onMTUChange(uint16_t mtu, NimBLEConnInfo &connInfo) override {
        (void)connInfo;
        Serial.printf("MTU changed to: %u\n", mtu);
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    // BLE client writes data -> forward to UART
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
        (void)connInfo;
        NimBLEAttValue value = pCharacteristic->getValue();
        if (value.length() > 0) {
            Serial2.write(value.data(), value.length());
            // Debug: log to USB console
            Serial.printf("BLE RX (%d bytes): ", value.length());
            Serial.write(value.data(), value.length());
            Serial.println();
#ifdef NUS_ECHO_TEST
            // Loopback for testing without UART2 wiring.
            // Do NOT enable when UART2 is connected — the Mac side will read
            // its own outbound zenoh-serial frames before the remote responds.
            pTxCharacteristic->setValue(value.data(), value.length());
            pTxCharacteristic->notify();
#endif
        }
    }
};

// ----- Setup -----
void setup() {
    // USB debug console (UART0)
    Serial.begin(115200);
    Serial.println("\n=== zenoh-ble-bridge starting ===");

    // Data UART (UART2) -- connects to Linux host
    Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    Serial.printf("UART2 initialized: %d baud, RX=%d TX=%d\n",
                  UART_BAUD_RATE, UART_RX_PIN, UART_TX_PIN);

    // LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Initialize BLE
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(512);  // Request large MTU (negotiated down by client)
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Max TX power

    // Create BLE server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Create NUS service
    NimBLEService *pService = pServer->createService(NUS_SERVICE_UUID);

    // TX characteristic (ESP32 -> BLE client, notify)
    pTxCharacteristic = pService->createCharacteristic(
        NUS_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX characteristic (BLE client -> ESP32, write)
    NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        NUS_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    // Start service
    pService->start();

    // Start advertising
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
    pAdvertising->setName(DEVICE_NAME);
    pAdvertising->start();

    Serial.println("BLE advertising started. Waiting for connection...");
    Serial.printf("Device name: %s\n", DEVICE_NAME);
    Serial.printf("NUS Service: %s\n", NUS_SERVICE_UUID);
}

// ----- Main Loop -----
void loop() {
    updateLed();

    // Handle reconnection: restart advertising after disconnect
    if (!deviceConnected && oldDeviceConnected) {
        delay(100);  // Brief delay for BLE stack cleanup
        NimBLEDevice::getAdvertising()->start();
        Serial.println("Restarted advertising after disconnect.");
        oldDeviceConnected = false;
    }
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = true;
    }

    // UART -> BLE: read available UART data and send as BLE notifications
    if (deviceConnected) {
        size_t available = Serial2.available();
        if (available > 0) {
            if (available > UART_BUF_SIZE) {
                available = UART_BUF_SIZE;
            }
            size_t bytesRead = Serial2.readBytes(uartBuf, available);

            // Send in chunks that fit in BLE MTU
            size_t offset = 0;
            while (offset < bytesRead) {
                size_t chunkLen = bytesRead - offset;
                if (chunkLen > BLE_CHUNK_SIZE) {
                    chunkLen = BLE_CHUNK_SIZE;
                }
                pTxCharacteristic->setValue(uartBuf + offset, chunkLen);
                pTxCharacteristic->notify();
                offset += chunkLen;

                // Small delay between chunks to avoid BLE congestion
                if (offset < bytesRead) {
                    delay(2);
                }
            }
        }
    }

    // Small yield to prevent watchdog timeout
    delay(1);
}
