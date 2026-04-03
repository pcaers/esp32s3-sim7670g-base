#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    bool advertising;
    int  connections;    // number of active BLE connections
    char device_name[32];
} ble_status_t;

// Initialize NimBLE stack and start advertising.
// Device name: "ESP32S3-Base"
esp_err_t ble_init(void);

// Copy current BLE status (thread-safe).
void ble_get_status(ble_status_t *out);
