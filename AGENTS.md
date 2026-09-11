# AGENTS.md

ESP32-C3 GNSS Stratum-1 NTP firmware (Arduino / PlatformIO). Hardware: 合宙 CORE ESP32-C3, 大夏龙雀 DX-GP22, SSD1306 OLED, KY-040 encoder, on-board LEDs D4/D5.

## Commands

```bash
pio run
pio run -t upload
pio device monitor
```

Env in `platformio.ini`: `esp32-c3` (`esp32-c3-devkitm-1`, Arduino). Serial: 115200, USB CDC on boot (`ARDUINO_USB_CDC_ON_BOOT=1`).

Client check after GPS lock + PPS: `ntpdate -q <device-ip>` — expect stratum 1, refid `GPSS`.

## Layout

| Path | Role |
|------|------|
| `include/config.h` | Pins, baud, NTP port, AP prefix, timeouts |
| `src/main.cpp` | Globals, `setup`/`loop`, WiFi connect + IP-conflict apply |
| `gps_service` | NMEA UART + PPS ISR, UTC for NTP |
| `ntp_server` | UDP/123, LI/stratum, timestamps |
| `wifi_manager` | STA/AP, scan, ARP IP conflict |
| `web_portal` | SoftAP portal on port 80 |
| `encoder` / `display_ui` | KY-040 + OLED menu |
| `status_leds` | D4 network, D5 GNSS/PPS |
| `settings` | NVS namespace `ntp-srv` |

Headers in `include/`, implementations in `src/`. One class per pair.

## Pins (`config.h`)

UART0 调试 (Serial 115200): RX 20, TX 21（板载 CH343；`ARDUINO_USB_CDC_ON_BOOT=0`）。GNSS UART1 9600: RX 1, TX 0, PPS 4。OLED I2C: SDA 8, SCL 10, addr `0x3C`。Encoder: A 2, B 3, SW 5。LEDs active HIGH: D4=12, D5=13。GPS 调试开关：`GPS_DEBUG` / `GPS_DEBUG_NMEA`。

Do not hardcode pins in `.cpp`; use `config.h` macros.

## Architecture

- FreeRTOS three tasks (ESP32-C3 single core): `task-time` prio 5 (UART1/PPS/UDP 123), `task-net` prio 2 (WiFi/Web:80), `task-ui` prio 1 (OLED/encoder/LEDs). `loop()` deletes itself after spawn.
- PPS ISR notifies time via `vTaskNotifyGiveFromISR`; time also polls every 1ms.
- Cross-task IPC in `app_ipc`: `NetRequest` / `UiMsg` queues + `gSettings` mutex. UI never blocks on WiFi scan/connect.
- GPS `nowUtc()` uses PPS-count lag vs NMEA commit (`seconds = utcEpoch + lag`) to avoid -1s jump while RMC trails PPS. Commit only on new NMEA second; stall >3s → refuse time. RX buffer 2048.
- NTP honest metadata: unsync → LI=3/stratum 16; PPS ready → precision -10; dispersion from `qualityMs`.
- SoftAP `NTP-Setup-XXXX` / `12345678` when not on STA (or menu Web Setup). HTTP `/` status, `/setup` WiFi, `/status` JSON.

## Conventions

- Arduino C++11-ish: `#pragma once`, classes with `begin()`/`loop()`, trailing underscore members.
- ISRs (`IRAM_ATTR`): PPS and encoder A only. Keep them short; share state via `volatile`. Encoder rotate is consumed with `noInterrupts()`.
- LEDs: HIGH = on. D4: AP ~4 Hz, no STA ~1 Hz, STA solid. D5: off / blink / solid as in README.
- Settings persist with `Preferences` keys: `ssid`, `pass`, `static`, `ip`, `gw`, `mask`, `dns`, `tz`. Default timezone +8.
- Libs: ArduinoJson 7, Adafruit SSD1306/GFX, TinyGPSPlus — versions pinned in `platformio.ini`.

## Do not

- Do not add blocking waits in `loop()` (except existing short `delay` after WiFi join).
- Do not use `millis()`-unsafe heavy work in ISRs.
- Do not treat `mktime` as UTC in GPS code; epoch is computed from NMEA date/time as UTC.
- Do not change SoftAP password or NTP refid (`GPSS`) without updating README.
- Do not commit `.pio/` or IDE browse DBs (see `.gitignore`).
