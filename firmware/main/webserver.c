#include "webserver.h"
#include "dashboard.h"
#include "camera.h"
#include "sim7670.h"
#include "ble.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "webserver";

// ── Dashboard ─────────────────────────────────────────────────────────────

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

// ── Status API ────────────────────────────────────────────────────────────

static esp_err_t status_handler(httpd_req_t *req) {
    wifi_status_t  wifi = {0};
    sim7670_status_t sim = {0};
    ble_status_t   ble  = {0};

    wifi_manager_get_status(&wifi);
    sim7670_get_status(&sim);
    ble_get_status(&ble);

    // System info
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    size_t free_heap  = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char *chip_model = "ESP32-S3";

    // SD card info
    bool sd_mounted = false;
    int64_t sd_free_mb = 0;
    struct stat st;
    if (stat("/sdcard", &st) == 0) {
        sd_mounted = true;
        FATFS *fs;
        DWORD free_clusters;
        if (f_getfree("0:", &free_clusters, &fs) == FR_OK) {
            sd_free_mb = (int64_t)free_clusters * fs->csize * 512 / (1024 * 1024);
        }
    }

    cJSON *root = cJSON_CreateObject();

    // WiFi
    cJSON *w = cJSON_CreateObject();
    cJSON_AddNumberToObject(w, "mode",         (double)wifi.mode);
    cJSON_AddStringToObject(w, "ap_ip",        wifi.ap_ip);
    cJSON_AddNumberToObject(w, "ap_clients",   wifi.ap_clients);
    cJSON_AddBoolToObject  (w, "sta_connected",wifi.sta_connected);
    cJSON_AddStringToObject(w, "sta_ssid",     wifi.sta_ssid);
    cJSON_AddStringToObject(w, "sta_ip",       wifi.sta_ip);
    cJSON_AddNumberToObject(w, "sta_rssi",     wifi.sta_rssi);
    cJSON_AddItemToObject(root, "wifi", w);

    // SIM
    cJSON *s = cJSON_CreateObject();
    cJSON_AddBoolToObject  (s, "initialized",   sim.initialized);
    cJSON_AddBoolToObject  (s, "registered",    sim.registered);
    cJSON_AddNumberToObject(s, "csq",           sim.csq);
    cJSON_AddNumberToObject(s, "rssi_dbm",      sim.rssi_dbm);
    cJSON_AddStringToObject(s, "operator_name", sim.operator_name);
    cJSON_AddStringToObject(s, "ip",            sim.ip);
    cJSON_AddItemToObject(root, "sim", s);

    // BLE
    cJSON *b = cJSON_CreateObject();
    cJSON_AddBoolToObject  (b, "advertising",  ble.advertising);
    cJSON_AddNumberToObject(b, "connections",  ble.connections);
    cJSON_AddStringToObject(b, "device_name",  ble.device_name);
    cJSON_AddItemToObject(root, "ble", b);

    // System
    cJSON *sy = cJSON_CreateObject();
    cJSON_AddNumberToObject(sy, "uptime_s",   uptime_s);
    cJSON_AddNumberToObject(sy, "free_heap",  free_heap);
    cJSON_AddNumberToObject(sy, "total_heap", total_heap);
    cJSON_AddStringToObject(sy, "chip_model", chip_model);
    cJSON_AddBoolToObject  (sy, "sd_mounted", sd_mounted);
    cJSON_AddNumberToObject(sy, "sd_free_mb", sd_free_mb);
    cJSON_AddItemToObject(root, "system", sy);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

// ── WiFi config API ───────────────────────────────────────────────────────

static esp_err_t wifi_post_handler(httpd_req_t *req) {
    char buf[256] = {0};
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    if (httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        return ESP_FAIL;
    }

    cJSON *body = cJSON_ParseWithLength(buf, len);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    cJSON *j_ssid = cJSON_GetObjectItem(body, "ssid");
    cJSON *j_pass = cJSON_GetObjectItem(body, "password");
    if (!cJSON_IsString(j_ssid) || !cJSON_IsString(j_pass)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid and password required");
        return ESP_FAIL;
    }

    const char *ssid = j_ssid->valuestring;
    const char *pass = j_pass->valuestring;

    esp_err_t ret = wifi_manager_set_sta(ssid, pass);
    cJSON_Delete(body);

    cJSON *resp = cJSON_CreateObject();
    if (ret == ESP_OK) {
        wifi_status_t st = {0};
        wifi_manager_get_status(&st);
        cJSON_AddStringToObject(resp, "status", "connected");
        cJSON_AddStringToObject(resp, "ip", st.sta_ip);
        char *r = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, r, strlen(r));
        free(r);
    } else {
        httpd_resp_set_status(req, "502 Bad Gateway");
        cJSON_AddStringToObject(resp, "error", "connection failed");
        char *r = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, r, strlen(r));
        free(r);
    }
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t wifi_delete_handler(httpd_req_t *req) {
    wifi_manager_clear_sta();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"disconnected\"}");
    return ESP_OK;
}

// ── Start server ──────────────────────────────────────────────────────────

esp_err_t webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.stack_size       = 16384;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",            .method = HTTP_GET,    .handler = root_handler         },
        { .uri = "/stream",      .method = HTTP_GET,    .handler = camera_stream_handler},
        { .uri = "/snapshot",    .method = HTTP_GET,    .handler = camera_snapshot_handler},
        { .uri = "/api/status",  .method = HTTP_GET,    .handler = status_handler       },
        { .uri = "/api/wifi",    .method = HTTP_POST,   .handler = wifi_post_handler    },
        { .uri = "/api/wifi",    .method = HTTP_DELETE, .handler = wifi_delete_handler  },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    ESP_LOGI(TAG, "Dashboard: http://%s/", WIFI_AP_IP);
    return ESP_OK;
}
