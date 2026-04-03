#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t camera_init(void);

// HTTP handler: GET /stream  — sends MJPEG multipart stream
esp_err_t camera_stream_handler(httpd_req_t *req);

// HTTP handler: GET /snapshot — returns a single JPEG frame
esp_err_t camera_snapshot_handler(httpd_req_t *req);
