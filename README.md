# ESP32S3-SIM7670G Base

Reusable base firmware for the **Waveshare ESP32-S3-SIM7670G-4G** board.  
Provides a local debug dashboard over WiFi for camera preview, 4G/BLE/WiFi status, and system info.

## Features

| Feature | Details |
|---------|---------|
| Camera | OV5640 live MJPEG stream + snapshot |
| WiFi | AP mode always on (`ESP32S3-Base` / `esp32s3base`), STA configurable via dashboard |
| 4G | SIM7670G status polling (signal, operator, IP) |
| Bluetooth | BLE 5.0 advertising as `ESP32S3-Base` |
| Dashboard | Single-page web UI at `http://192.168.4.1/` |

## Quick Start

```bash
cd firmware
idf.py set-target esp32s3
idf.py build flash monitor
```

1. Connect to WiFi **`ESP32S3-Base`** (password: `esp32s3base`)
2. Open **`http://192.168.4.1/`** in your browser

## Dashboard

- **Camera stream**: live MJPEG at `/stream`, pause/resume, single snapshot
- **WiFi**: AP status + form to connect to your router (saved to NVS)
- **4G**: SIM7670G signal strength, operator, PDP IP — polled every 15 s
- **BLE**: advertising status, connected device count
- **System**: uptime, free heap, SD card space

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Dashboard HTML |
| GET | `/stream` | MJPEG camera stream |
| GET | `/snapshot` | Single JPEG frame |
| GET | `/api/status` | JSON status of all subsystems |
| POST | `/api/wifi` | `{"ssid":"…","password":"…"}` — connect STA |
| DELETE | `/api/wifi` | Disconnect STA and clear saved credentials |

## GPIO Map (Waveshare ESP32-S3-SIM7670G-4G)

See [`firmware/main/board.h`](firmware/main/board.h) for the full pin table.

| Peripheral | Key pins |
|------------|----------|
| OV5640 camera | D0–D7: GPIO 7–14, XCLK: 39, VSYNC: 42, HREF: 41, PCLK: 46 |
| SD card | CLK: 5, CMD: 4, D0: 6 |
| SIM7670G | TX: 18, RX: 17 (UART1, 115200 baud) |
| Status LED | GPIO 2 |
| NeoPixel RGB | GPIO 38 |
| Free GPIOs | 1, 2, 3, 19, 20, 21, 45, 47, 48 |

## Requirements

- ESP-IDF ≥ 5.0
- Board: Waveshare ESP32-S3-SIM7670G-4G
- Camera: OV5640 (included with board)
- SIM card (optional — 4G status shows offline if not present)

## Adapting for Your Project

Use this as a starting point — the modules are independent:
- Remove `sim7670.c` if you don't need 4G
- Remove `ble.c` if you don't need Bluetooth
- Remove `camera.c` if you don't need video
- Add your application logic in `main.c` after `webserver_start()`
