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

enum class NtpAclMode : uint8_t {
  Off = 0,        // all clients (still subject to B1 rate limit)
  AllowList = 1,  // only listed IPv4; others silent drop
};

inline const char* ntpAclModeMenuLabel(NtpAclMode m) {
  switch (m) {
    case NtpAclMode::AllowList:
      return "AllowList";
    case NtpAclMode::Off:
    default:
      return "Off";
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
  // Empty → derived "NTP-" + MAC low 16-bit hex (NVS appw / webpw).
  String apPassword;
  String webPassword;
  // B3 NTP ACL (NVS aclm / acln / acl0..acl7); default Off.
  NtpAclMode ntpAclMode = NtpAclMode::Off;
  uint8_t ntpAclCount = 0;
  IPAddress ntpAcl[NTP_ACL_MAX_ENTRIES];
  // Optional crystal temp trim (NVS tcmp / tcpc). Default Off.
  bool tempComp = CLK_TEMP_COMP_DEFAULT;
  int16_t tempCoeffCenti = CLK_TEMP_COEFF_CENTI;  // ppm/°C × 100
  // OLED idle blanking timeout in ms; 0 = always on (NVS ooff).
  uint32_t oledIdleOffMs = OLED_IDLE_OFF_DEFAULT_MS;
};

// Fixed pick list shared by OLED menu + web config (ms values).
constexpr uint32_t kOledIdleOptions[] = {0, 60000, 300000, 600000, 1800000};
constexpr uint8_t kOledIdleOptionCount =
    sizeof(kOledIdleOptions) / sizeof(kOledIdleOptions[0]);
constexpr uint8_t kOledIdleDefaultIdx = 3;  // 10 min

inline const char* oledIdleMenuLabel(uint32_t ms) {
  switch (ms) {
    case 0:
      return "Always";
    case 60000:
      return "1 min";
    case 300000:
      return "5 min";
    case 1800000:
      return "30 min";
    case 600000:
    default:
      return "10 min";
  }
}

inline uint8_t oledIdleIndexForMs(uint32_t ms) {
  for (uint8_t i = 0; i < kOledIdleOptionCount; ++i) {
    if (kOledIdleOptions[i] == ms) {
      return i;
    }
  }
  return kOledIdleDefaultIdx;
}

inline float tempCoeffPpmPerC(int16_t centi) {
  return static_cast<float>(centi) / 100.0f;
}

// SoftAP / web-write password helpers (B2).
inline String derivedSoftApPassword() {
  char buf[12];
  snprintf(buf, sizeof(buf), "NTP-%04X",
           static_cast<unsigned>(static_cast<uint32_t>(ESP.getEfuseMac()) & 0xFFFFu));
  return String(buf);
}

inline String effectiveSoftApPassword(const AppSettings& s) {
  if (!s.apPassword.isEmpty()) {
    return s.apPassword;
  }
  return derivedSoftApPassword();
}

inline String effectiveWebWritePassword(const AppSettings& s) {
  if (!s.webPassword.isEmpty()) {
    return s.webPassword;
  }
  return effectiveSoftApPassword(s);
}

class SettingsStore {
 public:
  void begin();
  AppSettings load() const;
  void save(const AppSettings& s) const;
  // Factory reset: wipe every key in the settings namespace (WiFi, passwords,
  // static IP, tz, policy, ACL, temp comp, oled idle, ver/crc).
  void reset();

 private:
  mutable Preferences prefs_;
};
