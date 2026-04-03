#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Waveshare ESP32-S3-SIM7670G-4G Board GPIO Map
// Source: Waveshare ESP32-S3-A-SIM7670X-4G-V2 official examples
// ═══════════════════════════════════════════════════════════════════════════

// ── OV5640 Camera (DVP parallel interface) ───────────────────────────────
#define CAM_PIN_PWDN    -1   // not used on this board
#define CAM_PIN_RESET   -1   // not used on this board
#define CAM_PIN_XCLK    39
#define CAM_PIN_SIOD    15   // SCCB data (shared with I2C)
#define CAM_PIN_SIOC    16   // SCCB clock (shared with I2C)
#define CAM_PIN_D0       7   // Y2
#define CAM_PIN_D1       8   // Y3
#define CAM_PIN_D2       9   // Y4
#define CAM_PIN_D3      10   // Y5
#define CAM_PIN_D4      11   // Y6
#define CAM_PIN_D5      12   // Y7
#define CAM_PIN_D6      13   // Y8
#define CAM_PIN_D7      14   // Y9
#define CAM_PIN_VSYNC   42
#define CAM_PIN_HREF    41
#define CAM_PIN_PCLK    46

// ── SD Card (SDMMC 1-bit mode) ───────────────────────────────────────────
#define SD_CLK_PIN       5
#define SD_CMD_PIN       4
#define SD_D0_PIN        6

// ── SIM7670G 4G Modem (UART1) ────────────────────────────────────────────
#define SIM_UART        UART_NUM_1
#define SIM_TX_PIN      18
#define SIM_RX_PIN      17
#define SIM_BAUD        115200

// ── NeoPixel RGB LED ─────────────────────────────────────────────────────
#define LED_RGB_PIN     38   // WS2812B data

// ── Status LED (simple GPIO) ─────────────────────────────────────────────
#define LED_STATUS_PIN   2   // free GPIO, use as simple output LED

// ── Battery Monitor MAX17048 (I2C, shared bus with camera SCCB) ──────────
#define BAT_I2C_SDA     15
#define BAT_I2C_SCL     16
#define BAT_I2C_ADDR    0x36

// ── Free GPIOs (safe to use for PIR, relays, etc.) ───────────────────────
// GPIO 1, 2, 3, 19, 20, 21, 45, 47, 48
// Note: 43/44 are USB serial (UART0 TX/RX), avoid those

// ── AP default credentials (change in wifi_manager.c or via dashboard) ───
#define WIFI_AP_SSID    "ESP32S3-Base"
#define WIFI_AP_PASS    "esp32s3base"
#define WIFI_AP_IP      "192.168.4.1"

// ── Webserver ─────────────────────────────────────────────────────────────
#define WEBSERVER_PORT  80
