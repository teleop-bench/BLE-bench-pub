// ESP32 L2CAP CoC Echo Server
//
// Tests BLE L2CAP Connection-Oriented Channel transport.
// The ESP32 acts as a BLE peripheral, creates an L2CAP CoC server on PSM 0x80,
// and echoes back any data received on the channel.
//
// This bypasses GATT entirely — no services, no characteristics.
// L2CAP CoC provides a socket-like interface with credit-based flow control
// and SDU boundaries, giving 10-50 KB/s throughput (vs 2-6 KB/s for NUS).
//
// Uses NimBLE-Arduino 2.x C++ L2CAP API (NimBLEL2CAPServer/Channel).

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEL2CAPServer.h>
#include <NimBLEL2CAPChannel.h>

// ----- Configuration -----
#define DEVICE_NAME       "zenoh-l2cap"
#define L2CAP_PSM         0x0080     // First dynamic LE PSM
#define L2CAP_MTU         512        // SDU size
#define LED_PIN           2

// ----- State -----
static NimBLEL2CAPChannel *g_channel = nullptr;
static uint32_t g_rx_bytes = 0;
static uint32_t g_tx_bytes = 0;
static uint32_t g_rx_count = 0;
static volatile bool g_stream_requested = false;
static volatile bool g_sink_mode = false;  // receive-only, no echo

// ----- LED -----
static unsigned long lastLedToggle = 0;
static bool ledState = false;

void updateLed() {
    if (g_channel && g_channel->isConnected()) {
        digitalWrite(LED_PIN, HIGH);
    } else {
        unsigned long now = millis();
        if (now - lastLedToggle >= 300) {
            lastLedToggle = now;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        }
    }
}

// ----- L2CAP Callbacks -----
class EchoCallbacks : public NimBLEL2CAPChannelCallbacks {
    bool shouldAcceptConnection(NimBLEL2CAPChannel *channel) override {
        Serial.println("L2CAP: incoming connection, accepting.");
        return true;
    }

    void onConnect(NimBLEL2CAPChannel *channel, uint16_t negotiatedMTU) override {
        g_channel = channel;
        Serial.printf("L2CAP CONNECTED (mtu=%u)\n", negotiatedMTU);
    }

    void onRead(NimBLEL2CAPChannel *channel, std::vector<uint8_t> &data) override {
        g_rx_bytes += data.size();
        g_rx_count++;

        // Only log every 50th SDU during bulk receives — Serial.printf blocks
        // the NimBLE host task when the UART buffer fills, preventing L2CAP
        // credit updates from being sent back to the remote.
        if (g_rx_count <= 5 || g_rx_count % 50 == 0) {
            Serial.printf("L2CAP RX (%u bytes, total=%u, count=%u)\n",
                           data.size(), g_rx_bytes, g_rx_count);
        }

        // Check for streaming command: "STREAM" triggers unidirectional burst
        if (data.size() == 6 && memcmp(data.data(), "STREAM", 6) == 0) {
            Serial.println("STREAM mode requested — will send from loop()");
            g_stream_requested = true;
            return;
        }

        // "SINK\0\0" (6 bytes padded) or just check first 4
        if (data.size() >= 4 && memcmp(data.data(), "SINK", 4) == 0) {
            g_sink_mode = true;
            Serial.println("SINK mode: receive-only, no echo");
            return;
        }

        // "ECHO" restores echo mode
        if (data.size() >= 4 && memcmp(data.data(), "ECHO", 4) == 0) {
            g_sink_mode = false;
            Serial.println("ECHO mode restored");
            return;
        }

        // In sink mode, just count — don't echo back.
        // Echoing back allocates TX mbufs from the same pool as RX,
        // causing pool exhaustion and preventing L2CAP credit updates.
        if (g_sink_mode) {
            return;
        }

        // Echo back
        if (channel->write(data)) {
            g_tx_bytes += data.size();
        } else {
            Serial.println("  TX FAILED");
        }
    }

    void onDisconnect(NimBLEL2CAPChannel *channel) override {
        Serial.println("L2CAP DISCONNECTED");
        g_channel = nullptr;
    }
};

// ----- Server Callbacks -----
class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
        (void)pServer;
        Serial.printf("GAP connected: %s\n",
                       connInfo.getAddress().toString().c_str());
        pServer->updateConnParams(connInfo.getConnHandle(), 6, 12, 0, 400);
        // Enable Data Length Extension (DLE): 251-byte PDUs instead of 27-byte default.
        // Without this, a 480-byte SDU fragments into ~24 tiny packets (2-3x throughput hit).
        pServer->setDataLen(connInfo.getConnHandle(), 251);
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
        (void)pServer;
        (void)connInfo;
        Serial.printf("GAP disconnected (reason=%d)\n", reason);
        g_channel = nullptr;
        NimBLEDevice::getAdvertising()->start();
        Serial.println("Restarted advertising.");
    }

    void onMTUChange(uint16_t mtu, NimBLEConnInfo &connInfo) override {
        (void)connInfo;
        Serial.printf("ATT MTU changed to %u\n", mtu);
    }
};

// ----- Setup -----
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== zenoh-l2cap echo server starting ===");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Initialize NimBLE
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(512);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Create BLE server for GAP
    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCB());

    // Create L2CAP CoC server
    NimBLEL2CAPServer *l2capServer = NimBLEDevice::createL2CAPServer();
    NimBLEL2CAPChannel *svc = l2capServer->createService(
        L2CAP_PSM, L2CAP_MTU, new EchoCallbacks());

    if (svc) {
        Serial.printf("L2CAP CoC server on PSM=0x%04X, MTU=%u\n",
                       L2CAP_PSM, L2CAP_MTU);
    } else {
        Serial.println("ERROR: Failed to create L2CAP service!");
        Serial.println("Check CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM > 0");
    }

    // Start advertising
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(DEVICE_NAME);
    pAdvertising->start();

    Serial.printf("BLE advertising as: %s\n", DEVICE_NAME);
    Serial.printf("Address: %s\n",
                   NimBLEDevice::getAddress().toString().c_str());
    Serial.println("Waiting for L2CAP connection...");
}

// ----- Main Loop -----
void loop() {
    updateLed();

    // Handle streaming outside the callback so the BLE stack task is free
    // to process L2CAP credit flow while we send.
    // Snapshot channel pointer — onDisconnect() can null g_channel from the
    // NimBLE host task (core 0) while this loop runs on the Arduino task (core 1).
    NimBLEL2CAPChannel *chan = g_channel;
    if (g_stream_requested && chan && chan->isConnected()) {
        g_stream_requested = false;
        Serial.println("STREAM mode: sending 200 x 480-byte SDUs...");
        std::vector<uint8_t> payload(480, 'S');
        uint32_t sent = 0;
        uint32_t failed = 0;
        uint32_t t0 = millis();
        for (int i = 0; i < 200; i++) {
            if (!chan->isConnected()) {
                Serial.println("STREAM aborted: disconnected");
                break;
            }
            payload[0] = (i >> 0) & 0xFF;
            payload[1] = (i >> 8) & 0xFF;
            payload[2] = (i >> 16) & 0xFF;
            payload[3] = (i >> 24) & 0xFF;
            if (chan->write(payload)) {
                sent++;
                g_tx_bytes += payload.size();
            } else {
                failed++;
            }
            // Yield so BLE stack can process credits and TX queue
            delay(2);
        }
        uint32_t elapsed = millis() - t0;
        Serial.printf("STREAM done: %u/%d SDUs (%u failed) in %u ms (%.1f KB/s)\n",
                       sent, 200, failed, elapsed,
                       elapsed > 0 ? (sent * 480.0f) / elapsed : 0.0f);
    }

    static unsigned long lastStats = 0;
    if (g_channel && g_channel->isConnected() && millis() - lastStats >= 5000) {
        lastStats = millis();
        Serial.printf("[stats] rx=%u bytes (%u SDUs), tx=%u bytes\n",
                       g_rx_bytes, g_rx_count, g_tx_bytes);
    }

    delay(10);
}
