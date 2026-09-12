#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <Preferences.h>
#include "config.h"

enum class AnomalyPolicy : uint8_t {
  Refuse = 0,         // immediately unsync
  HoldoverShort = 1,  // short local holdover
  HoldoverLong = 2,   // longer local holdover
};

inline uint16_t anomalyPolicyDefaultHoldoverSec(AnomalyPolicy p) {
  switch (p) {
    case AnomalyPolicy::HoldoverShort:
      return CLK_HOLDOVER_SHORT_SEC;
    case AnomalyPolicy::HoldoverLong:
      return CLK_HOLDOVER_LONG_SEC;
    case AnomalyPolicy::Refuse:
    default:
      return 0;
  }
}

inline const char* anomalyPolicyShortLabel(AnomalyPolicy p) {
  switch (p) {
    case AnomalyPolicy::HoldoverShort:
      return "H30";
    case AnomalyPolicy::HoldoverLong:
      return "H5m";
    case AnomalyPolicy::Refuse:
    default:
      return "REF";
  }
}

inline const char* anomalyPolicyMenuLabel(AnomalyPolicy p) {
  switch (p) {
    case AnomalyPolicy::HoldoverShort:
      return "Hold 30s";
    case AnomalyPolicy::HoldoverLong:
      return "Hold 5m";
    case AnomalyPolicy::Refuse:
    default:
      return "Refuse";
  }
}

struct AppSettings {
  String wifiSsid;
  String wifiPass;
  bool useStaticIp = false;
  IPAddress staticIp{192, 168, 1, 50};
  IPAddress gateway{192, 168, 1, 1};
  IPAddress subnet{255, 255, 255, 0};
  IPAddress dns{8, 8, 8, 8};
  int8_t timezoneHours = 8;
  AnomalyPolicy anomalyPolicy = AnomalyPolicy::Refuse;
  uint16_t holdoverSec = CLK_HOLDOVER_SHORT_SEC;
  bool autoReconnect = true;  // NVS key arec
};

class SettingsStore {
 public:
  void begin();
  AppSettings load() const;
  void save(const AppSettings& s) const;

 private:
  mutable Preferences prefs_;
};
