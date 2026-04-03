#include "sim7670.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sim7670";

#define BUF_SIZE      4096
#define POLL_INTERVAL_S  15

static sim7670_status_t s_status = {0};
static SemaphoreHandle_t s_mutex;

// ── Low-level UART helpers ────────────────────────────────────────────────

static void uart_send(const char *s) {
    uart_write_bytes(SIM_UART, s, strlen(s));
    uart_write_bytes(SIM_UART, "\r\n", 2);
    ESP_LOGD(TAG, ">>> %s", s);
}

static bool uart_wait(uint32_t timeout_ms, const char *a, const char *b,
                      const char *c, const char *d) {
    static uint8_t buf[BUF_SIZE];
    int used = 0;
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) < timeout_ms) {
        int n = uart_read_bytes(SIM_UART, buf + used,
                                (BUF_SIZE - 1) - used, 50 / portTICK_PERIOD_MS);
        if (n > 0) {
            used += n;
            buf[used] = 0;
            ESP_LOGD(TAG, "<<< %s", (char *)buf);
            if (a && strstr((char *)buf, a)) return true;
            if (b && strstr((char *)buf, b)) return true;
            if (c && strstr((char *)buf, c)) return true;
            if (d && strstr((char *)buf, d)) return true;
            if (used > (BUF_SIZE * 3 / 4)) used = 0;
        }
    }
    return false;
}

// Send command, return true if expected response received
bool sim7670_cmd(const char *cmd, const char *expect, uint32_t timeout_ms) {
    uart_send(cmd);
    return uart_wait(timeout_ms, expect, "ERROR", "+CME ERROR", NULL);
}

// ── Status polling ────────────────────────────────────────────────────────

// Parse CSQ response: "+CSQ: 18,0" → csq=18, rssi=-75
static void parse_csq(const char *resp, sim7670_status_t *st) {
    int csq = 99;
    if (sscanf(resp, "+CSQ: %d", &csq) == 1 ||
        sscanf(resp, "+CSQ:%d", &csq) == 1) {
        st->csq = csq;
        st->rssi_dbm = (csq == 99) ? 0 : (-113 + csq * 2);
    }
}

// Parse COPS response: +COPS: 0,0,"Proximus",7
static void parse_cops(const char *resp, sim7670_status_t *st) {
    char name[32] = {0};
    if (sscanf(resp, "+COPS: %*d,%*d,\"%31[^\"]\"", name) == 1) {
        strncpy(st->operator_name, name, sizeof(st->operator_name) - 1);
    }
}

// Parse CGPADDR response: +CGPADDR: 1,10.x.x.x
static void parse_ip(const char *resp, sim7670_status_t *st) {
    char ip[20] = {0};
    if (sscanf(resp, "+CGPADDR: %*d,%19s", ip) == 1) {
        strncpy(st->ip, ip, sizeof(st->ip) - 1);
    }
}

void sim7670_poll(void) {
    static uint8_t rx[BUF_SIZE];
    sim7670_status_t tmp = {0};
    tmp.initialized = true;

    // Signal quality
    uart_send("AT+CSQ");
    int used = 0;
    uint32_t t0 = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - t0) < 3000) {
        int n = uart_read_bytes(SIM_UART, rx + used, (BUF_SIZE - 1) - used,
                                50 / portTICK_PERIOD_MS);
        if (n > 0) { used += n; rx[used] = 0; }
        if (strstr((char *)rx, "OK") || strstr((char *)rx, "ERROR")) break;
    }
    parse_csq((char *)rx, &tmp);

    // Network registration
    uart_send("AT+CGREG?");
    used = 0;
    t0 = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - t0) < 3000) {
        int n = uart_read_bytes(SIM_UART, rx + used, (BUF_SIZE - 1) - used,
                                50 / portTICK_PERIOD_MS);
        if (n > 0) { used += n; rx[used] = 0; }
        if (strstr((char *)rx, "OK") || strstr((char *)rx, "ERROR")) break;
    }
    // +CGREG: 0,1 or +CGREG: 0,5 means registered
    tmp.registered = (strstr((char *)rx, ",1") || strstr((char *)rx, ",5"));

    // Operator name
    uart_send("AT+COPS?");
    used = 0;
    t0 = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - t0) < 5000) {
        int n = uart_read_bytes(SIM_UART, rx + used, (BUF_SIZE - 1) - used,
                                50 / portTICK_PERIOD_MS);
        if (n > 0) { used += n; rx[used] = 0; }
        if (strstr((char *)rx, "OK") || strstr((char *)rx, "ERROR")) break;
    }
    parse_cops((char *)rx, &tmp);

    // PDP IP
    uart_send("AT+CGPADDR=1");
    used = 0;
    t0 = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - t0) < 3000) {
        int n = uart_read_bytes(SIM_UART, rx + used, (BUF_SIZE - 1) - used,
                                50 / portTICK_PERIOD_MS);
        if (n > 0) { used += n; rx[used] = 0; }
        if (strstr((char *)rx, "OK") || strstr((char *)rx, "ERROR")) break;
    }
    parse_ip((char *)rx, &tmp);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status = tmp;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "4G: csq=%d (%d dBm), reg=%d, op='%s', ip=%s",
             tmp.csq, tmp.rssi_dbm, tmp.registered, tmp.operator_name, tmp.ip);
}

void sim7670_get_status(sim7670_status_t *out) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_mutex);
}

// ── Background polling task ───────────────────────────────────────────────

static void poll_task(void *arg) {
    while (1) {
        sim7670_poll();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_S * 1000));
    }
}

// ── Init ─────────────────────────────────────────────────────────────────

esp_err_t sim7670_init(void) {
    s_mutex = xSemaphoreCreateMutex();

    uart_config_t cfg = {
        .baud_rate  = SIM_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(SIM_UART, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SIM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SIM_UART, SIM_TX_PIN, SIM_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    uart_flush(SIM_UART);

    vTaskDelay(pdMS_TO_TICKS(2000)); // give modem time to boot

    if (!sim7670_cmd("AT", "OK", 3000)) {
        ESP_LOGW(TAG, "Modem not responding — check wiring and power");
        // still start poll task; modem may become available later
    } else {
        sim7670_cmd("ATE0", "OK", 1000); // disable echo
        ESP_LOGI(TAG, "SIM7670G ready");
        s_status.initialized = true;
    }

    xTaskCreate(poll_task, "sim_poll", 4096, NULL, 3, NULL);
    return ESP_OK;
}
