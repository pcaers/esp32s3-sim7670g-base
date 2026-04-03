#include "webserver.h"
#include "dashboard.h"
#include "camera.h"
#include "sim7670.h"
#include "ble.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "webserver";

// ── Dashboard ─────────────────────────────────────────────────────────────

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

// ── Status API ────────────────────────────────────────────────────────────

static esp_err_t status_handler(httpd_req_t *req) {
    wifi_status_t    wifi = {0};
    sim7670_status_t sim  = {0};
    ble_status_t     ble  = {0};

    wifi_manager_get_status(&wifi);
    sim7670_get_status(&sim);
    ble_get_status(&ble);

    uint32_t uptime_s  = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    size_t   free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t   total_heap= heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    // SD card info
    bool    sd_mounted = false;
    int64_t sd_free_mb = 0;
    struct stat st;
    if (stat("/sdcard", &st) == 0) {
        sd_mounted = true;
        FATFS *fs;
        DWORD  free_clusters;
        if (f_getfree("0:", &free_clusters, &fs) == FR_OK) {
            sd_free_mb = (int64_t)free_clusters * fs->csize * 512 / (1024 * 1024);
        }
    }

    // Build JSON manually — avoids any cJSON dependency
    char json[1024];
    snprintf(json, sizeof(json),
        "{"
          "\"wifi\":{"
            "\"mode\":%d,"
            "\"ap_ip\":\"%s\","
            "\"ap_clients\":%d,"
            "\"sta_connected\":%s,"
            "\"sta_ssid\":\"%s\","
            "\"sta_ip\":\"%s\","
            "\"sta_rssi\":%d"
          "},"
          "\"sim\":{"
            "\"initialized\":%s,"
            "\"registered\":%s,"
            "\"csq\":%d,"
            "\"rssi_dbm\":%d,"
            "\"operator_name\":\"%s\","
            "\"ip\":\"%s\""
          "},"
          "\"ble\":{"
            "\"advertising\":%s,"
            "\"connections\":%d,"
            "\"device_name\":\"%s\""
          "},"
          "\"system\":{"
            "\"uptime_s\":%lu,"
            "\"free_heap\":%zu,"
            "\"total_heap\":%zu,"
            "\"chip_model\":\"ESP32-S3\","
            "\"sd_mounted\":%s,"
            "\"sd_free_mb\":%lld"
          "}"
        "}",
        (int)wifi.mode, wifi.ap_ip, wifi.ap_clients,
        wifi.sta_connected ? "true" : "false",
        wifi.sta_ssid, wifi.sta_ip, wifi.sta_rssi,
        sim.initialized ? "true" : "false",
        sim.registered  ? "true" : "false",
        sim.csq, sim.rssi_dbm, sim.operator_name, sim.ip,
        ble.advertising ? "true" : "false",
        ble.connections, ble.device_name,
        (unsigned long)uptime_s, free_heap, total_heap,
        sd_mounted ? "true" : "false", sd_free_mb
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// ── WiFi config API ───────────────────────────────────────────────────────

static esp_err_t wifi_post_handler(httpd_req_t *req) {
    char body[256] = {0};
    int  len       = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    if (httpd_req_recv(req, body, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        return ESP_FAIL;
    }

    // Parse minimal JSON: {"ssid":"...","password":"..."}
    char ssid[33] = {0};
    char pass[65] = {0};

    char *p = strstr(body, "\"ssid\"");
    if (p) {
        p = strchr(p, ':');
        if (p) { p = strchr(p, '"'); }
        if (p) { sscanf(p + 1, "%32[^\"]", ssid); }
    }
    p = strstr(body, "\"password\"");
    if (p) {
        p = strchr(p, ':');
        if (p) { p = strchr(p, '"'); }
        if (p) { sscanf(p + 1, "%64[^\"]", pass); }
    }

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    if (wifi_manager_set_sta(ssid, pass) == ESP_OK) {
        wifi_status_t st = {0};
        wifi_manager_get_status(&st);
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"connected\",\"ip\":\"%s\"}", st.sta_ip);
        httpd_resp_send(req, resp, strlen(resp));
    } else {
        httpd_resp_set_status(req, "502 Bad Gateway");
        httpd_resp_sendstr(req, "{\"error\":\"connection failed\"}");
    }
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
    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.server_port     = 80;
    config.stack_size      = 16384;
    config.max_open_sockets= 7;
    config.lru_purge_enable= true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",           .method = HTTP_GET,    .handler = root_handler          },
        { .uri = "/stream",     .method = HTTP_GET,    .handler = camera_stream_handler },
        { .uri = "/snapshot",   .method = HTTP_GET,    .handler = camera_snapshot_handler },
        { .uri = "/api/status", .method = HTTP_GET,    .handler = status_handler        },
        { .uri = "/api/wifi",   .method = HTTP_POST,   .handler = wifi_post_handler     },
        { .uri = "/api/wifi",   .method = HTTP_DELETE, .handler = wifi_delete_handler   },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server started - dashboard: http://192.168.4.1/");
    return ESP_OK;
}
