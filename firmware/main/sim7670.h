#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    bool  initialized;
    bool  registered;      // registered on mobile network
    int   csq;             // raw CSQ value (0-31, 99=unknown)
    int   rssi_dbm;        // signal strength in dBm
    char  operator_name[32];
    char  ip[20];          // PDP context IP
} sim7670_status_t;

esp_err_t sim7670_init(void);

// Poll AT commands and update internal status.
// Called by background task every POLL_INTERVAL_S seconds.
void sim7670_poll(void);

// Copy current status into out (thread-safe snapshot).
void sim7670_get_status(sim7670_status_t *out);

// Send a raw AT command and wait for response (blocking, for one-off use).
bool sim7670_cmd(const char *cmd, const char *expect, uint32_t timeout_ms);
