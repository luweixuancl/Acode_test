#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <vector>
#include "settings.h"

struct WifiNetwork {
  String ssid;
  int32_t rssi = 0;
  wifi_auth_mode_t enc = WIFI_AUTH_OPEN;
};

class WifiManager {
 public:
  // Init radio only — do not block on STA connect (net task connects).
  void begin();
  bool connectSta(const AppSettings& settings);
  void startSetupAp();
  void stopAp();
  bool isStaConnected() const;
  IPAddress localIp() const;
  String macAddress() const;

  std::vector<WifiNetwork> scanNetworks();
  bool detectIpConflict(const IPAddress& ip);
  bool applyStaticIp(const AppSettings& settings);

 private:
  bool pingProbe(const IPAddress& ip);
};
