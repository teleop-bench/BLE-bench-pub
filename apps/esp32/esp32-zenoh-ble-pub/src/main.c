//
// ESP32 zenoh-pico publisher over BLE L2CAP
//
// The ESP32 acts as a BLE peripheral:
//   1. Initializes NimBLE and starts advertising
//   2. Opens a zenoh-pico peer session listening on BLE L2CAP
//   3. Publishes periodic messages on "test/ble"
//
// The central (Lemur) runs z_ble_sub to connect and subscribe.
//

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_system.h>
#include <os/os_mempool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>

#include <zenoh-pico.h>

#define DEVICE_NAME "zenoh-ble"
#define KEYEXPR     "test/ble"
#define VALUE       "Hello from ESP32 over BLE!"

static const char *TAG = "zenoh_ble_pub";
static uint8_t own_addr_type;
static SemaphoreHandle_t ble_ready_sem;

// ---------------------------------------------------------------------------
// GAP: advertising & connection management
// ---------------------------------------------------------------------------

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "GAP connected handle=%u",
                         event->connect.conn_handle);
                // Enable Data Length Extension for better L2CAP throughput
                ble_gap_set_data_len(event->connect.conn_handle, 251, 2120);
                // Request longer supervision timeout (BlueZ defaults to 420ms
                // which is too aggressive for USB adapters — see bluez/bluez#847).
                // Keep interval at BlueZ defaults to avoid rejection.
                struct ble_gap_upd_params conn_params = {
                    .itvl_min = 0x0018,              // 30ms  (match BlueZ default)
                    .itvl_max = 0x0028,              // 50ms  (match BlueZ default)
                    .latency = 0,
                    .supervision_timeout = 0x0200,   // 5120ms (was 420ms)
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                int rc = ble_gap_update_params(event->connect.conn_handle,
                                               &conn_params);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Connection param update request failed: %d", rc);
                }
            } else {
                ESP_LOGW(TAG, "GAP connect failed status=%d",
                         event->connect.status);
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "GAP disconnected reason=%d",
                     event->disconnect.reason);
            start_advertising();
            break;

        case BLE_GAP_EVENT_CONN_UPDATE: {
            struct ble_gap_conn_desc desc;
            int rc2 = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            if (rc2 == 0) {
                ESP_LOGI(TAG, "CONN_UPDATE status=%d interval=%u latency=%u "
                         "supervision_timeout=%u",
                         event->conn_update.status,
                         desc.conn_itvl, desc.conn_latency,
                         desc.supervision_timeout);
            } else {
                ESP_LOGI(TAG, "CONN_UPDATE status=%d (conn_find failed rc=%d)",
                         event->conn_update.status, rc2);
            }
            break;
        }

        case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
            // Log what the peer is requesting (or what we requested)
            const struct ble_gap_upd_params *peer =
                event->conn_update_req.peer_params;
            ESP_LOGI(TAG, "CONN_UPDATE_REQ itvl=%u-%u latency=%u timeout=%u",
                     peer->itvl_min, peer->itvl_max,
                     peer->latency, peer->supervision_timeout);
            // Accept by copying peer params to self_params
            *event->conn_update_req.self_params = *peer;
            break;
        }

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "ATT MTU updated to %u",
                     event->mtu.value);
            break;

        default:
            ESP_LOGD(TAG, "GAP event type=%d", event->type);
            break;
    }
    return 0;
}

static void start_advertising(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    }
}

// ---------------------------------------------------------------------------
// NimBLE host callbacks
// ---------------------------------------------------------------------------

static void ble_on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    uint8_t addr[6];
    ble_hs_id_copy_addr(own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    start_advertising();
    ESP_LOGI(TAG, "Advertising as '%s'", DEVICE_NAME);

    xSemaphoreGive(ble_ready_sem);
}

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset: reason=%d", reason);
}

static void ble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------------------
// Main: NimBLE init → zenoh session → publish loop
// ---------------------------------------------------------------------------

void app_main(void) {
    // NVS (required by NimBLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ble_ready_sem = xSemaphoreCreateBinary();

    // Initialize NimBLE
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);
    nimble_port_freertos_init(ble_host_task);

    // Wait for BLE ready (on_sync fires)
    ESP_LOGI(TAG, "Waiting for BLE stack...");
    xSemaphoreTake(ble_ready_sem, portMAX_DELAY);
    vSemaphoreDelete(ble_ready_sem);

    // Open zenoh session as peer, listening on BLE L2CAP
    ESP_LOGI(TAG, "Opening zenoh session (listening on BLE)...");
    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY,
                     "ble/*#psm=128;tout=0");

    z_owned_session_t session;
    if (z_open(&session, z_move(config), NULL) < 0) {
        ESP_LOGE(TAG, "Failed to open zenoh session!");
        return;
    }
    ESP_LOGI(TAG, "Zenoh session opened!");

    // Start background tasks for session keepalive
    zp_start_read_task(z_loan_mut(session), NULL);
    zp_start_lease_task(z_loan_mut(session), NULL);

    // Declare publisher
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str_unchecked(&ke, KEYEXPR);
    z_owned_publisher_t pub;
    if (z_declare_publisher(z_loan(session), &pub, z_loan(ke), NULL) < 0) {
        ESP_LOGE(TAG, "Failed to declare publisher for '%s'", KEYEXPR);
        z_drop(z_move(session));
        return;
    }
    ESP_LOGI(TAG, "Publishing on '%s'", KEYEXPR);

    // Publish loop with mbuf pool monitoring
    char buf[128];
    for (uint32_t idx = 0;; idx++) {
        snprintf(buf, sizeof(buf), "[%4lu] %s", (unsigned long)idx, VALUE);

        // Log mbuf pool status every 10 iterations
        if (idx % 10 == 0) {
            int num_free = os_msys_num_free();
            int count = os_msys_count();
            ESP_LOGI(TAG, "[%lu] mbuf free=%d/%d heap=%lu",
                     (unsigned long)idx, num_free, count,
                     (unsigned long)esp_get_free_heap_size());
        }

        z_owned_bytes_t payload;
        z_bytes_copy_from_str(&payload, buf);
        z_publisher_put(z_loan(pub), z_move(payload), NULL);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
