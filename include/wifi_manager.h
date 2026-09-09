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
  void begin(const AppSettings& settings);
  bool connectSta(const AppSettings& settings);
  void startSetupAp();
  void stopAp();
  bool isStaConnected() const;
  IPAddress localIp() const;
  String macAddress() const;

  std::vector<WifiNetwork> scanNetworks();
  // Returns true if another host already uses this IPv4 on the LAN.
  bool detectIpConflict(const IPAddress& ip);
  bool applyStaticIp(const AppSettings& settings);

 private:
  bool pingProbe(const IPAddress& ip);
};
