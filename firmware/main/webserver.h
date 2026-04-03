#pragma once
#include "esp_err.h"

// Start the HTTP server on port 80.
// Registers: GET /  GET /stream  GET /snapshot
//            GET /api/status  POST /api/wifi  DELETE /api/wifi
esp_err_t webserver_start(void);
