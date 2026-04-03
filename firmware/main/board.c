#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board";

void board_led_set(int level) {
    gpio_set_level(LED_STATUS_PIN, level);
}

void board_led_init(void) {
    gpio_config_t io = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LED_STATUS_PIN,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    board_led_set(0);
    ESP_LOGI(TAG, "Status LED ready (GPIO %d)", LED_STATUS_PIN);
}
