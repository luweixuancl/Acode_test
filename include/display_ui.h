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
  // Call only from task-ui. Reads GPS via snapshot; posts NetRequests for WiFi.
  void loop(EncoderInput& enc, GpsService& gps, WifiManager& wifi);

  void showMessage(const String& msg);
  void onScanResults(const std::vector<WifiNetwork>& nets);

 private:
  void drawHome(const GpsStatus& st, const WifiManager& wifi, const AppSettings& settings);
  void drawMenu();
  void drawWifiScan();
  void drawPassword();
  void drawSetIp();
  void drawTimezone(const AppSettings& settings);
  void drawMessage();
  void drawWebHint();

  void handleHome(int8_t rot, bool click);
  void handleMenu(int8_t rot, bool click, bool longPress);
  void handleWifiScan(int8_t rot, bool click);
  void handlePassword(int8_t rot, bool click, bool longPress);
  void handleSetIp(int8_t rot, bool click, bool longPress);
  void handleTimezone(int8_t rot, bool click);
  void drainUiMessages();

  Adafruit_SSD1306 display_{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
  UiMode mode_ = UiMode::Home;
  uint8_t menuIndex_ = 0;
  uint8_t wifiIndex_ = 0;
  std::vector<WifiNetwork> networks_;
  bool scanPending_ = false;
  String password_;
  uint8_t pwdCursor_ = 0;
  uint8_t ipOctet_ = 0;
  IPAddress editIp_;
  String pendingSsid_;
  String message_;
  uint32_t messageUntil_ = 0;
  uint32_t lastDrawMs_ = 0;
};
