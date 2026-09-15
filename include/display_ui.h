#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <vector>
#include "config.h"
#include "gps_service.h"
#include "settings.h"
#include "wifi_manager.h"
#include "encoder.h"

class NtpServer;

enum class UiMode : uint8_t {
  Home,
  Menu,
  WifiScan,
  WifiPassword,
  SetIp,
  SetTimezone,
  SetAnomaly,
  SetAcl,
  SetTempComp,
  NtpStats,
  WebSetupHint,
  Message,
};

enum class MenuItem : uint8_t {
  WifiScan = 0,
  WebSetup,
  SetStaticIp,
  UseDhcp,
  Timezone,
  AnomalyMode,
  NtpAcl,
  TempComp,
  NtpStats,
  Restart,
  Count
};

class DisplayUi {
 public:
  void begin();
  void loop(EncoderInput& enc, GpsService& gps, WifiManager& wifi, NtpServer& ntp);

  void showMessage(const String& msg);
  void onScanResults(const std::vector<WifiNetwork>& nets);

 private:
  void drawHome(const GpsStatus& st, const WifiManager& wifi, const AppSettings& settings);
  void drawMenu();
  void drawWifiScan();
  void drawPassword();
  void drawSetIp();
  void drawTimezone(const AppSettings& settings);
  void drawAnomaly(const AppSettings& settings);
  void drawAcl(const AppSettings& settings);
  void drawTempComp(const AppSettings& settings);
  void drawNtpStats(const NtpServer& ntp);
  void drawMessage();
  void drawWebHint();

  void handleHome(int8_t rot, bool click);
  void handleMenu(int8_t rot, bool click, bool longPress, WifiManager& wifi);
  void handleWifiScan(int8_t rot, bool click, bool longPress);
  void handlePassword(int8_t rot, bool click, bool longPress);
  void handleSetIp(int8_t rot, bool click, bool longPress);
  void handleTimezone(int8_t rot, bool click);
  void handleAnomaly(int8_t rot, bool click, bool longPress);
  void handleAcl(int8_t rot, bool click, bool longPress);
  void handleTempComp(int8_t rot, bool click, bool longPress);
  void handleNtpStats(int8_t rot, bool click, bool longPress);
  void drainUiMessages();
  void requestWifiScan();
  void screenOn();
  void screenOff();

  Adafruit_SH1107 display_{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
  UiMode mode_ = UiMode::Home;
  uint8_t menuIndex_ = 0;
  uint8_t wifiIndex_ = 0;
  std::vector<WifiNetwork> networks_;
  bool scanPending_ = false;
  uint32_t scanSeq_ = 0;
  uint32_t scanStartedMs_ = 0;
  String scanError_;
  String password_;
  uint8_t pwdCursor_ = 0;
  uint8_t ipOctet_ = 0;
  IPAddress editIp_;
  AnomalyPolicy editPolicy_ = AnomalyPolicy::Refuse;
  NtpAclMode editAclMode_ = NtpAclMode::Off;
  uint8_t editAclCount_ = 0;
  bool editTempComp_ = false;
  String pendingSsid_;
  String message_;
  uint32_t messageUntil_ = 0;
  uint32_t lastDrawMs_ = 0;
  bool screenOff_ = false;
  uint32_t lastInputMs_ = 0;
};
