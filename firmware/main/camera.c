#include "camera.h"
#include "board.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "camera";

esp_err_t camera_init(void) {
    camera_config_t cfg = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d0       = CAM_PIN_D0,
        .pin_d1       = CAM_PIN_D1,
        .pin_d2       = CAM_PIN_D2,
        .pin_d3       = CAM_PIN_D3,
        .pin_d4       = CAM_PIN_D4,
        .pin_d5       = CAM_PIN_D5,
        .pin_d6       = CAM_PIN_D6,
        .pin_d7       = CAM_PIN_D7,
        .pin_vsync    = CAM_PIN_VSYNC,
        .pin_href     = CAM_PIN_HREF,
        .pin_pclk     = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_VGA,    // 640x480; change to QVGA for lower latency
        .jpeg_quality = 12,               // 0–63, lower = better quality
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "OV5640 ready (VGA, JPEG q=%d)", cfg.jpeg_quality);
    return ESP_OK;
}

esp_err_t camera_stream_handler(httpd_req_t *req) {
    static const char boundary[] = "--frame\r\nContent-Type: image/jpeg\r\n";

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char hdr[64];
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Frame capture failed, retrying");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", fb->len);

        esp_err_t err = ESP_OK;
        err |= httpd_resp_send_chunk(req, boundary, strlen(boundary));
        err |= httpd_resp_send_chunk(req, hdr, strlen(hdr));
        err |= httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        err |= httpd_resp_send_chunk(req, "\r\n", 2);

        esp_camera_fb_return(fb);

        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Client disconnected from stream");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(80)); // ~12 fps
    }
    return ESP_OK;
}

esp_err_t camera_snapshot_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snapshot.jpg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return ESP_OK;
}
