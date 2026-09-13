# AGENTS.md

ESP32-C3 GNSS Stratum-1 NTP firmware (Arduino / PlatformIO). Hardware: 合宙 CORE ESP32-C3, 大夏龙雀 DX-GP22, SH1107/SSD1107 64×128 OLED (UI rotated 128×64), KY-040 encoder, on-board LEDs D4/D5.

## Commands

```bash
pio run
pio run -t upload
pio device monitor
```

Env in `platformio.ini`: `esp32-c3` (`esp32-c3-devkitm-1`, Arduino). Serial: 115200, UART0 CH343 (`ARDUINO_USB_CDC_ON_BOOT=0`).

Client check after GPS lock + PPS: `ntpdate -q <device-ip>` — expect stratum 1, refid `GPSS`.

## Layout

| Path | Role |
|------|------|
| `include/config.h` | Pins, baud, NTP port, AP prefix, timeouts |
| `src/main.cpp` | Globals, `setup`/`loop`, three FreeRTOS tasks |
| `local_clock` | PPS-disciplined UTC, residual FSM, Holdover |
| `gps_service` | NMEA UART + PPS ISR, owns LocalClock |
| `ntp_server` | UDP/123, LI/stratum, timestamps |
| `wifi_manager` | STA/AP, 事件驱动连接/扫描, 退避重连, ARP IP conflict |
| `web_portal` | SoftAP portal on port 80 |
| `encoder` / `display_ui` | KY-040 + OLED menu |
| `status_leds` | D4 network, D5 GNSS/PPS |
| `settings` | NVS namespace `ntp-srv` |
| `docs/local_clock_gps_check.md` | LocalClock + AnomalyPolicy design (已实现) |
| `docs/wifi_event_fsm.md` | WiFi 事件 FSM、重连、C3 无双核说明 |
| `docs/ntp_cmp_test_20260911.md` | 10min NTP 比对：GPS vs 本机/阿里云 |

Headers in `include/`, implementations in `src/`. One class per pair.

## Pins (`config.h`)

UART0 调试 (Serial 115200): RX 20, TX 21（板载 CH343；`ARDUINO_USB_CDC_ON_BOOT=0`）。GNSS UART1 9600: RX 1, TX 0, PPS 4。OLED I2C SH1107 64×128: SDA 8, SCL 10, addr `0x3C`（`OLED_ROTATION=1` → 逻辑 128×64）。Encoder: A 2, B 3, SW 5。LEDs active HIGH: D4=12, D5=13。GPS 调试开关：`GPS_DEBUG` / `GPS_DEBUG_NMEA`。

Do not hardcode pins in `.cpp`; use `config.h` macros.

## Architecture

- FreeRTOS three tasks (**ESP32-C3 single-core only** — no APP/PRO dual-core split on this board): `task-time` prio 5 (UART1/PPS/UDP 123), `task-net` prio 2 (WiFi/Web:80), `task-ui` prio 1 (OLED/encoder/LEDs). All pinned to core 0. `loop()` deletes itself after spawn. Dual-core pinning is a future ESP32-S3 roadmap item only; see `docs/wifi_event_fsm.md`.
- PPS ISR notifies time via `vTaskNotifyGiveFromISR`; time also polls every 1ms.
- Cross-task IPC in `app_ipc`: `NetRequest` / `UiMsg` queues + `gSettings` mutex. UI never blocks on WiFi scan/connect.
- GPS owns `LocalClock`: PPS edges via `esp_timer`, ppm EMA, LocalUtc extrapolation; NMEA residual cross-check (warn 50 ms / fail 100 ms). States ACQ/LCK/DEG/HLD/UNS. `nowUtc()` serves NTP only from Locked/Degraded/Holdover.
- AnomalyPolicy in NVS (`apol`/`ahold`): Refuse / HoldoverShort(30s) / HoldoverLong(300s); OLED Anomaly Mode + Web `/setup`.
- NTP honest metadata: unsync → LI=3/stratum 16/refid `INIT`; Locked/Degraded/Holdover LI=0 (LI is leap-second only); sync only LCK/DEG/HLD; PPS ready → precision -10; Reference Timestamp = last PPS-aligned second; dispersion from `qualityMs` (holdover: entry + max(EMA,50ppm,PHI)×age, cap → UNS).
- NTP B1 rate limit: per-IP `NTP_RATE_PER_IP_PER_SEC` with KoD `RATE`; sustained abuse → KoD `DENY` then silent drop for `NTP_DENY_COOLDOWN_MS`; global `NTP_GLOBAL_RATE_PER_SEC` silent drop. Counters on `/status` (`served`/`rateLimited`/`denied`/`dropped`/`clients`).
- SoftAP `NTP-Setup-XXXX` / `12345678` when **no saved STA SSID** (or reconnect give-up / menu Web Setup). HTTP `/` status, `/setup` WiFi+policy (shows saved SSID + one-tap reconnect), `/status` JSON (`clock`/`residualMs`/`anomalyPolicy`/`savedSsid`/`freeHeap`).
- Settings NVS: CRC mismatch **never clears WiFi** (only refreshes CRC); flash updates should write app @ `0x10000` without full-chip erase to keep NVS.
- Stability: PPS ISR queue drains multi-edges; task-net/ui on TWDT + LED-stale soft-restart; scan does not cancel STA reconnect; Holdover dispersion uses ppm×age; NVS `ver`/`crc` guards settings.
- `WifiManager`: `WiFi.onEvent` only sets flags/logs; `task-net` consumes GOT_IP/DISC/SCAN_DONE. Connect success prefers GOT_IP (fallback WL_CONNECTED+IP). STA drop → backoff auto-reconnect (`WIFI_RECONNECT_*`, NVS `arec` default on); give-up opens SoftAP. Scan results cached in `lastScan_` for OLED + `/scan`.
- `task-net` polls STA connect / WiFi scan / ARP conflict without blocking; HTTP `handleClient` keeps running during join/scan.
- Design: `docs/local_clock_gps_check.md`, `docs/wifi_event_fsm.md`, `docs/ntp_cmp_test_20260911.md`.
- Windows note: project path with non-ASCII may break `ld` map file; build via ASCII junction (e.g. `C:\acode_leds`) if link fails.

## Conventions

- Arduino C++11-ish: `#pragma once`, classes with `begin()`/`loop()`, trailing underscore members.
- ISRs (`IRAM_ATTR`): PPS plus encoder A **and** B (CHANGE). Keep them short; share state via `volatile`. Encoder uses `esp_timer_get_time()` debounce (no `millis()` in ISR); rotate is consumed with `noInterrupts()`.
- LEDs: HIGH = on. D4: AP ~4 Hz, no STA ~1 Hz, STA heartbeat (~900/100 ms). D5: off / ACQ blink / HLD fast blink / LCK·DEG heartbeat. Alternate panic blink if any task kick goes stale (~3 s).
- Settings persist with `Preferences` keys: `ssid`, `pass`, `static`, `ip`, `gw`, `mask`, `dns`, `tz`, `apol`, `ahold`, `arec` (auto-reconnect, default true). Default timezone +8; anomaly Refuse.
- Libs: ArduinoJson 7, Adafruit SH110X/GFX, TinyGPSPlus — versions pinned in `platformio.ini`.

## Do not

- Do not add blocking waits in `loop()` (except existing short `delay` after WiFi join).
- Do not use `millis()`-unsafe heavy work in ISRs.
- Do not treat `mktime` as UTC in GPS code; epoch is computed from NMEA date/time as UTC.
- Do not change SoftAP password or NTP refid (`GPSS`) without updating README.
- Do not commit `.pio/` or IDE browse DBs (see `.gitignore`).
