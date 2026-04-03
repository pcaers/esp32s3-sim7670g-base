#include "ble.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG      = "ble";
static const char *DEV_NAME = "ESP32S3-Base";

static ble_status_t s_status = {
    .advertising  = false,
    .connections  = 0,
    .device_name  = "ESP32S3-Base",
};

// ── Advertising ───────────────────────────────────────────────────────────

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_status.connections++;
            ESP_LOGI(TAG, "BLE connected (total: %d)", s_status.connections);
        } else {
            // Connection failed — restart advertising
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_status.connections = (s_status.connections > 0) ? s_status.connections - 1 : 0;
        ESP_LOGI(TAG, "BLE disconnected (total: %d)", s_status.connections);
        start_advertising(); // restart advertising after disconnect
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    default:
        break;
    }
    return 0;
}

static void start_advertising(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags               = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name                = (uint8_t *)DEV_NAME;
    fields.name_len            = strlen(DEV_NAME);
    fields.name_is_complete    = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // connectable, undirected
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // general discoverable

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
        s_status.advertising = false;
    } else {
        s_status.advertising = true;
        ESP_LOGI(TAG, "Advertising as '%s'", DEV_NAME);
    }
}

// ── NimBLE host task ──────────────────────────────────────────────────────

static void ble_host_task(void *arg) {
    nimble_port_run();           // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }
    ble_svc_gap_device_name_set(DEV_NAME);
    start_advertising();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset, reason: %d", reason);
    s_status.advertising = false;
}

// ── Init ──────────────────────────────────────────────────────────────────

esp_err_t ble_init(void) {
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE (NimBLE) initialized");
    return ESP_OK;
}

void ble_get_status(ble_status_t *out) {
    *out = s_status;
}
