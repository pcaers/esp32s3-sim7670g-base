#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "board.h"
#include "camera.h"
#include "sim7670.h"
#include "ble.h"
#include "wifi_manager.h"
#include "webserver.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32S3-SIM7670G Base ===");

    // NVS — required for WiFi credentials storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition erased and re-initialized");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Status LED
    board_led_init();
    board_led_set(1); // on during init

    // Camera
    if (camera_init() != ESP_OK) {
        ESP_LOGW(TAG, "Camera init failed — stream endpoint will not work");
    }

    // SIM7670G (non-fatal — modem may not be fitted or powered)
    sim7670_init();

    // BLE
    if (ble_init() != ESP_OK) {
        ESP_LOGW(TAG, "BLE init failed");
    }

    // WiFi (AP always + STA if credentials are saved in NVS)
    ESP_ERROR_CHECK(wifi_manager_init());

    // HTTP server with dashboard
    ESP_ERROR_CHECK(webserver_start());

    board_led_set(0); // init complete

    ESP_LOGI(TAG, "Ready. Connect to WiFi '%s' and open http://%s/",
             WIFI_AP_SSID, WIFI_AP_IP);
}
