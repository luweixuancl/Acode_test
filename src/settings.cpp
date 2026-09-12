#include "settings.h"
#include <esp_crc.h>

namespace {

constexpr uint16_t kSettingsVer = 2;

uint32_t settingsCrc(const AppSettings& s) {
  // CRC over critical fields so a torn NVS write can be detected.
  uint32_t crc = 0;
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.wifiSsid.c_str()), s.wifiSsid.length());
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.wifiPass.c_str()), s.wifiPass.length());
  const uint8_t flags[] = {
      static_cast<uint8_t>(s.useStaticIp ? 1 : 0),
      static_cast<uint8_t>(s.timezoneHours),
      static_cast<uint8_t>(s.anomalyPolicy),
      static_cast<uint8_t>(s.autoReconnect ? 1 : 0),
  };
  crc = esp_crc32_le(crc, flags, sizeof(flags));
  const uint8_t ip[16] = {
      s.staticIp[0], s.staticIp[1], s.staticIp[2], s.staticIp[3],
      s.gateway[0],  s.gateway[1],  s.gateway[2],  s.gateway[3],
      s.subnet[0],   s.subnet[1],   s.subnet[2],   s.subnet[3],
      s.dns[0],      s.dns[1],      s.dns[2],      s.dns[3],
  };
  crc = esp_crc32_le(crc, ip, sizeof(ip));
  const uint16_t hold = s.holdoverSec;
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(&hold), sizeof(hold));
  return crc;
}

}  // namespace

void SettingsStore::begin() {
  prefs_.begin("ntp-srv", false);
}

AppSettings SettingsStore::load() const {
  AppSettings s;
  s.wifiSsid = prefs_.getString("ssid", "");
  s.wifiPass = prefs_.getString("pass", "");
  // Repair corrupt creds from older Web savePolicy bug (JSON null → "null").
  if (s.wifiSsid == "null" || s.wifiSsid == "undefined") {
    s.wifiSsid = "";
    s.wifiPass = "";
  }
  s.useStaticIp = prefs_.getBool("static", false);
  s.staticIp.fromString(prefs_.getString("ip", "192.168.1.50"));
  s.gateway.fromString(prefs_.getString("gw", "192.168.1.1"));
  s.subnet.fromString(prefs_.getString("mask", "255.255.255.0"));
  s.dns.fromString(prefs_.getString("dns", "8.8.8.8"));
  s.timezoneHours = static_cast<int8_t>(prefs_.getInt("tz", 8));

  const uint8_t apol = static_cast<uint8_t>(prefs_.getUChar("apol", 0));
  if (apol <= static_cast<uint8_t>(AnomalyPolicy::HoldoverLong)) {
    s.anomalyPolicy = static_cast<AnomalyPolicy>(apol);
  } else {
    s.anomalyPolicy = AnomalyPolicy::Refuse;
  }
  const uint16_t defHold = anomalyPolicyDefaultHoldoverSec(s.anomalyPolicy);
  s.holdoverSec = static_cast<uint16_t>(prefs_.getUShort("ahold", defHold ? defHold : CLK_HOLDOVER_SHORT_SEC));
  if (s.holdoverSec < 10) {
    s.holdoverSec = 10;
  }
  if (s.holdoverSec > 600) {
    s.holdoverSec = 600;
  }
  s.autoReconnect = prefs_.getBool("arec", true);

  const uint16_t ver = prefs_.getUShort("ver", 0);
  const uint32_t storedCrc = prefs_.getUInt("crc", 0);
  if (ver == kSettingsVer && storedCrc != 0) {
    const uint32_t calc = settingsCrc(s);
    if (calc != storedCrc) {
      Serial.printf("[settings] CRC mismatch stored=%08x calc=%08x — clear WiFi creds\n",
                    static_cast<unsigned>(storedCrc), static_cast<unsigned>(calc));
      s.wifiSsid = "";
      s.wifiPass = "";
      s.useStaticIp = false;
    }
  }
  return s;
}

void SettingsStore::save(const AppSettings& s) const {
  prefs_.putString("ssid", s.wifiSsid);
  prefs_.putString("pass", s.wifiPass);
  prefs_.putBool("static", s.useStaticIp);
  prefs_.putString("ip", s.staticIp.toString());
  prefs_.putString("gw", s.gateway.toString());
  prefs_.putString("mask", s.subnet.toString());
  prefs_.putString("dns", s.dns.toString());
  prefs_.putInt("tz", s.timezoneHours);
  prefs_.putUChar("apol", static_cast<uint8_t>(s.anomalyPolicy));
  prefs_.putUShort("ahold", s.holdoverSec);
  prefs_.putBool("arec", s.autoReconnect);
  prefs_.putUShort("ver", kSettingsVer);
  prefs_.putUInt("crc", settingsCrc(s));
}
