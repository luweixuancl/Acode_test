#pragma once

// ---------------------------------------------------------------------------
// Hardware wiring (ESP32-C3 + DX-GP22 + SSD1306 + KY-040)
// Adjust these pins if your carrier board differs.
// ---------------------------------------------------------------------------

// DX-GP22 GNSS (UART, default 9600 8N1; 1PPS after fix)
#define PIN_GPS_RX          20   // ESP32 RX <- GP22 TXD
#define PIN_GPS_TX          21   // ESP32 TX -> GP22 RXD
#define PIN_GPS_PPS          4   // 1PPS input
#define GPS_UART_BAUD     9600
#define GPS_UART_NUM         1

// SSD1306 128x64 OLED over I2C
#define PIN_OLED_SDA         8
#define PIN_OLED_SCL        10
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH         128
#define OLED_HEIGHT         64

// KY-040 rotary encoder
#define PIN_ENC_A            2
#define PIN_ENC_B            3
#define PIN_ENC_SW           5

// Status LEDs (active HIGH)
#define PIN_LED_D4          12   // RUN / WiFi
#define PIN_LED_D5          13   // GPS / PPS / NTP ready

// SoftAP for web WiFi setup
#define AP_SSID_PREFIX      "NTP-Setup"
#define AP_PASSWORD         "12345678"

// NTP
#define NTP_UDP_PORT         123
#define NTP_EPOCH_DELTA   2208988800UL  // 1900 -> 1970

// UI / timing
#define DISPLAY_REFRESH_MS   250
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define IP_CONFLICT_TIMEOUT_MS   800
