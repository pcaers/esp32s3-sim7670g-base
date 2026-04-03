#include "wifi_manager.h"
#include "board.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG    = "wifi";
static const char *NVS_NS = "wifi_cfg";

#define STA_CONNECTED_BIT  BIT0
#define STA_FAIL_BIT       BIT1
#define MAX_RETRY          5

static EventGroupHandle_t s_wifi_events;
static wifi_status_t      s_status   = {0};
static int                s_retries  = 0;

// ── Event handler ─────────────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_AP_STACONNECTED:
            s_status.ap_clients++;
            ESP_LOGI(TAG, "Client joined AP (total: %d)", s_status.ap_clients);
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            s_status.ap_clients = s_status.ap_clients > 0 ? s_status.ap_clients - 1 : 0;
            ESP_LOGI(TAG, "Client left AP (total: %d)", s_status.ap_clients);
            break;

        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_status.sta_connected = false;
            memset(s_status.sta_ip, 0, sizeof(s_status.sta_ip));
            if (s_retries < MAX_RETRY) {
                esp_wifi_connect();
                s_retries++;
                ESP_LOGI(TAG, "STA retry %d/%d", s_retries, MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_events, STA_FAIL_BIT);
                ESP_LOGW(TAG, "STA connection failed after %d retries", MAX_RETRY);
            }
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_status.sta_ip, sizeof(s_status.sta_ip),
                 IPSTR, IP2STR(&evt->ip_info.ip));
        s_status.sta_connected = true;
        s_retries = 0;
        xEventGroupSetBits(s_wifi_events, STA_CONNECTED_BIT);
        ESP_LOGI(TAG, "STA connected, IP: %s", s_status.sta_ip);
    }
}

// ── NVS helpers ───────────────────────────────────────────────────────────

static void nvs_save_sta(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

static bool nvs_load_sta(char *ssid, size_t ssid_len,
                          char *pass, size_t pass_len) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_str(h, "ssid", ssid, &ssid_len) == ESP_OK &&
               nvs_get_str(h, "pass", pass, &pass_len) == ESP_OK &&
               strlen(ssid) > 0);
    nvs_close(h);
    return ok;
}

static void nvs_clear_sta(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "pass");
    nvs_commit(h);
    nvs_close(h);
}

// ── Connect STA ───────────────────────────────────────────────────────────

static esp_err_t connect_sta(const char *ssid, const char *password) {
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retries = 0;
    xEventGroupClearBits(s_wifi_events, STA_CONNECTED_BIT | STA_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                            STA_CONNECTED_BIT | STA_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(15000));
    if (bits & STA_CONNECTED_BIT) {
        strncpy(s_status.sta_ssid, ssid, sizeof(s_status.sta_ssid) - 1);
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ── Public API ────────────────────────────────────────────────────────────

esp_err_t wifi_manager_init(void) {
    s_wifi_events = xEventGroupCreate();
    strncpy(s_status.ap_ip, WIFI_AP_IP, sizeof(s_status.ap_ip) - 1);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         wifi_event_handler, NULL, NULL);

    // Always start AP
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = WIFI_AP_SSID,
            .ssid_len       = strlen(WIFI_AP_SSID),
            .password       = WIFI_AP_PASS,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };

    char sta_ssid[33] = {0};
    char sta_pass[65] = {0};
    bool has_sta = nvs_load_sta(sta_ssid, sizeof(sta_ssid),
                                 sta_pass, sizeof(sta_pass));

    if (has_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        s_status.mode = APP_WIFI_MODE_APSTA;
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        s_status.mode = APP_WIFI_MODE_AP_ONLY;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP started: SSID='%s', IP=%s", WIFI_AP_SSID, WIFI_AP_IP);

    if (has_sta) {
        ESP_LOGI(TAG, "Connecting to saved STA: %s", sta_ssid);
        connect_sta(sta_ssid, sta_pass);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_set_sta(const char *ssid, const char *password) {
    // Switch to APSTA if not already
    wifi_mode_t current;
    esp_wifi_get_mode(&current);
    if (current == WIFI_MODE_AP) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    }
    s_status.mode = APP_WIFI_MODE_APSTA;

    esp_err_t ret = connect_sta(ssid, password);
    if (ret == ESP_OK) {
        nvs_save_sta(ssid, password);
        ESP_LOGI(TAG, "STA credentials saved for '%s'", ssid);
    }
    return ret;
}

esp_err_t wifi_manager_clear_sta(void) {
    nvs_clear_sta();
    esp_wifi_disconnect();
    s_status.sta_connected = false;
    memset(s_status.sta_ip,   0, sizeof(s_status.sta_ip));
    memset(s_status.sta_ssid, 0, sizeof(s_status.sta_ssid));
    s_status.mode = APP_WIFI_MODE_AP_ONLY;
    esp_wifi_set_mode(WIFI_MODE_AP);
    ESP_LOGI(TAG, "STA credentials cleared");
    return ESP_OK;
}

void wifi_manager_get_status(wifi_status_t *out) {
    // Read STA RSSI if connected
    if (s_status.sta_connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_status.sta_rssi = ap.rssi;
        }
    }
    *out = s_status;
}
