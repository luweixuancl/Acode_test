#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <Preferences.h>

struct AppSettings {
  String wifiSsid;
  String wifiPass;
  bool useStaticIp = false;
  IPAddress staticIp{192, 168, 1, 50};
  IPAddress gateway{192, 168, 1, 1};
  IPAddress subnet{255, 255, 255, 0};
  IPAddress dns{8, 8, 8, 8};
  int8_t timezoneHours = 8;  // CST default
};

class SettingsStore {
 public:
  void begin();
  AppSettings load() const;
  void save(const AppSettings& s) const;
  void clearWifi() const;

 private:
  mutable Preferences prefs_;
};
