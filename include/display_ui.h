#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <vector>
#include "config.h"
#include "gps_service.h"
#include "settings.h"
#include "wifi_manager.h"
#include "encoder.h"

enum class UiMode : uint8_t {
  Home,
  Menu,
  WifiScan,
  WifiPassword,
  SetIp,
  SetTimezone,
  WebSetupHint,
  Message,
};

enum class MenuItem : uint8_t {
  WifiScan = 0,
  WebSetup,
  SetStaticIp,
  UseDhcp,
  Timezone,
  Restart,
  Count
};

class DisplayUi {
 public:
  void begin();
  void loop(EncoderInput& enc,
            GpsService& gps,
            WifiManager& wifi,
            AppSettings& settings,
            SettingsStore& store);

  // Show transient status line for ~2s
  void showMessage(const String& msg);

  // Password editor helpers used by WiFi connect flow
  bool takePendingWifi(String& ssid, String& pass);
  bool takeApplyStaticIp();
  bool takeStartWebSetup();
  bool takeUseDhcp();

 private:
  void drawHome(const GpsService& gps, const WifiManager& wifi, const AppSettings& settings);
  void drawMenu();
  void drawWifiScan();
  void drawPassword();
  void drawSetIp();
  void drawTimezone(const AppSettings& settings);
  void drawMessage();
  void drawWebHint();

  void handleHome(int8_t rot, bool click);
  void handleMenu(int8_t rot, bool click, bool longPress, AppSettings& settings, SettingsStore& store);
  void handleWifiScan(int8_t rot, bool click, WifiManager& wifi);
  void handlePassword(int8_t rot, bool click, bool longPress);
  void handleSetIp(int8_t rot, bool click, bool longPress, AppSettings& settings, SettingsStore& store, WifiManager& wifi);
  void handleTimezone(int8_t rot, bool click, AppSettings& settings, SettingsStore& store);

  Adafruit_SSD1306 display_{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
  UiMode mode_ = UiMode::Home;
  uint8_t menuIndex_ = 0;
  uint8_t wifiIndex_ = 0;
  std::vector<WifiNetwork> networks_;
  String password_;
  uint8_t pwdCursor_ = 0;  // charset index
  uint8_t ipOctet_ = 0;
  IPAddress editIp_;
  String message_;
  uint32_t messageUntil_ = 0;
  uint32_t lastDrawMs_ = 0;

  String pendingSsid_;
  String pendingPass_;
  bool pendingWifi_ = false;
  bool pendingStatic_ = false;
  bool pendingWeb_ = false;
  bool pendingDhcp_ = false;
};
