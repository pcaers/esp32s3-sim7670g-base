#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    APP_WIFI_MODE_AP_ONLY = 0,
    APP_WIFI_MODE_APSTA,       // AP + STA simultaneously
} app_wifi_mode_t;

typedef struct {
    app_wifi_mode_t mode;
    char ap_ip[16];         // always "192.168.4.1"
    char sta_ip[16];        // empty string if not connected
    char sta_ssid[33];
    int  sta_rssi;
    bool sta_connected;
    int  ap_clients;        // number of stations connected to our AP
} wifi_status_t;

// Initialize WiFi. Reads config from NVS.
// Always starts AP mode. If STA credentials are saved, also connects as STA.
esp_err_t wifi_manager_init(void);

// Save new STA credentials to NVS and attempt connection.
// Returns ESP_OK if connection succeeded, ESP_FAIL otherwise.
esp_err_t wifi_manager_set_sta(const char *ssid, const char *password);

// Disconnect from STA and clear stored credentials.
esp_err_t wifi_manager_clear_sta(void);

// Copy current status (thread-safe).
void wifi_manager_get_status(wifi_status_t *out);
