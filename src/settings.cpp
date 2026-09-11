#include "settings.h"

void SettingsStore::begin() {
  prefs_.begin("ntp-srv", false);
}

AppSettings SettingsStore::load() const {
  AppSettings s;
  s.wifiSsid = prefs_.getString("ssid", "");
  s.wifiPass = prefs_.getString("pass", "");
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
}
