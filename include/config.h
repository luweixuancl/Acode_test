#pragma once

// ---------------------------------------------------------------------------
// Hardware wiring (合宙 CORE ESP32-C3 + DX-GP22 + SH1107/SSD1107 OLED + KY-040)
// Adjust these pins if your carrier board differs.
// ---------------------------------------------------------------------------

// UART0 (合宙 CORE / CH343): GPIO20 RX, GPIO21 TX — debug via Serial @ 115200
// DX-GP22 GNSS on UART1 (board UART1_RX=GPIO1, UART1_TX=GPIO0; 9600 8N1; 1PPS after fix)
#define PIN_GPS_RX           1   // ESP32 RX <- GP22 TXD  (UART1_RX)
#define PIN_GPS_TX           0   // ESP32 TX -> GP22 RXD  (UART1_TX)
#define PIN_GPS_PPS          4   // 1PPS input
#define GPS_UART_BAUD     9600
#define GPS_UART_NUM         1
#define GPS_DEBUG            0   // 1 = 每秒向 UART0 打印定位/PPS（time 任务内，默认关）
#define GPS_DEBUG_NMEA       0   // 1 = 把 NMEA 原文转发到 UART0

// SH1107 / SSD1107 0.96" 64x128 OLED over I2C (native portrait; setRotation(1) → 128x64 UI)
#define PIN_OLED_SDA         8
#define PIN_OLED_SCL        10
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH          64
#define OLED_HEIGHT        128
#define OLED_ROTATION        1   // 1 = landscape UI on portrait panel

// KY-040 rotary encoder
#define PIN_ENC_A            2
#define PIN_ENC_B            3
#define PIN_ENC_SW           5

// On-board LEDs on 合宙 CORE ESP32 (datasheet 表4-1): D4=IO12, D5=IO13, active HIGH
#define PIN_LED_D4          12   // D4 RUN / WiFi
#define PIN_LED_D5          13   // D5 GPS / PPS / NTP ready

// SoftAP for web WiFi setup
#define AP_SSID_PREFIX      "NTP-Setup"
#define AP_PASSWORD         "12345678"

// NTP
#define NTP_UDP_PORT         123
#define NTP_EPOCH_DELTA   2208988800UL  // 1900 -> 1970

// UI / timing
#define DISPLAY_REFRESH_MS   250
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define WIFI_NO_AP_REBEGIN_MS    3000  // re-WiFi.begin while Connecting after reason 201
#define WIFI_SCAN_TIMEOUT_MS    15000  // async scan must not hang reconnect give-up
#define IP_CONFLICT_TIMEOUT_MS   800
// Auto-reconnect backoff (ms): attempt 1 immediate, then 2s/5s/10s/30s...
#define WIFI_RECONNECT_MAX_ATTEMPTS    5
#define WIFI_RECONNECT_GIVEUP_MS  120000
#define WIFI_RECONNECT_BACKOFF_0_MS      0
#define WIFI_RECONNECT_BACKOFF_1_MS   2000
#define WIFI_RECONNECT_BACKOFF_2_MS   5000
#define WIFI_RECONNECT_BACKOFF_3_MS  10000
#define WIFI_RECONNECT_BACKOFF_4_MS  30000
// Boot: first STA fail arms reconnect; SoftAP only after give-up (not after 1x 201).
#define WIFI_BOOT_SOFTAP_AFTER_GIVEUP 1
#define GPS_NMEA_MAX_BYTES_PER_LOOP 256
#define NTP_MAX_PACKETS_PER_LOOP      8
// TinyGPSPlus isValid() stays true after last sentence; require fresh age + sats>0.
#define GPS_FIX_MAX_AGE_MS           5000

// Status LEDs: healthy "good" states use heartbeat (not solid) so hangs are visible
#define LED_HEARTBEAT_ON_MS         900
#define LED_HEARTBEAT_OFF_MS        100
#define LED_TASK_STALE_MS          3000
#define LED_PANIC_HALF_PERIOD_MS    100

// Local clock / GPS cross-check (see docs/local_clock_gps_check.md)
#define CLK_RESIDUAL_WARN_MS         50
#define CLK_RESIDUAL_FAIL_MS        100
#define CLK_RESIDUAL_RELOCK_MS       30
#define CLK_RELOCK_COUNT              3
#define CLK_PPS_INTERVAL_MAX_ERR_US 5000
#define CLK_PPS_UNSTABLE_COUNT        3
#define CLK_HOLDOVER_SHORT_SEC       30
#define CLK_HOLDOVER_LONG_SEC       300
#define CLK_PPS_EDGE_RING            16
#define CLK_PPM_EMA_ALPHA          0.2f
// Below this residual while Locked, keep PPS-only advance (no NMEA re-anchor).
#define CLK_LOCKED_SLEW_MS            5
// ISR→task PPS queue (missed edges under WiFi load).
#define GPS_PPS_ISR_QUEUE             8
// Missed PPS seconds ≥ this → Unsynced (not silent catch-up only).
#define CLK_PPS_MISS_UNSYNC           3
// Holdover dispersion: floor crystal error (ppm) when EMA is still small.
// Cheap MCU XO + unknown temp: 50 ppm is more honest than 20 for free-run bound.
#define CLK_HOLDOVER_PPM_FLOOR       50.0f
// NTP-like PHI (ppm): never grow slower than this during holdover.
#define CLK_HOLDOVER_PHI_PPM         15.0f
// Extra uncertainty booked when entering / while in holdover (ms).
#define CLK_HOLDOVER_ENTRY_MS       100
// If root-quality exceeds this in holdover, drop to Unsynced early.
#define CLK_HOLDOVER_MAX_QUALITY_MS 500
// Task panic (LED stale) for this long → soft restart.
#define LED_TASK_PANIC_RESTART_MS 15000
// Restart if free heap stays below this (fragmentation / leak).
#define HEAP_RESTART_BYTES         10240
#define HEAP_RESTART_SAMPLES            5

