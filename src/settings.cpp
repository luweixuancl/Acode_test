#include "settings.h"
#include <esp_crc.h>

namespace {

constexpr uint16_t kSettingsVer = 4;

uint32_t settingsCrc(const AppSettings& s) {
  // CRC over critical fields so a torn NVS write can be detected.
  uint32_t crc = 0;
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.wifiSsid.c_str()), s.wifiSsid.length());
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.wifiPass.c_str()), s.wifiPass.length());
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.apPassword.c_str()), s.apPassword.length());
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.webPassword.c_str()), s.webPassword.length());
  const uint8_t flags[] = {
      static_cast<uint8_t>(s.useStaticIp ? 1 : 0),
      static_cast<uint8_t>(s.timezoneHours),
      static_cast<uint8_t>(s.anomalyPolicy),
      static_cast<uint8_t>(s.autoReconnect ? 1 : 0),
      static_cast<uint8_t>(s.ntpAclMode),
      s.ntpAclCount,
      static_cast<uint8_t>(s.tempComp ? 1 : 0),
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
  const int16_t tc = s.tempCoeffCenti;
  crc = esp_crc32_le(crc, reinterpret_cast<const uint8_t*>(&tc), sizeof(tc));
  for (uint8_t i = 0; i < s.ntpAclCount && i < NTP_ACL_MAX_ENTRIES; ++i) {
    const uint8_t a[4] = {s.ntpAcl[i][0], s.ntpAcl[i][1], s.ntpAcl[i][2], s.ntpAcl[i][3]};
    crc = esp_crc32_le(crc, a, sizeof(a));
  }
  return crc;
}

}  // namespace

void SettingsStore::begin() {
  prefs_.begin("ntp-srv", false);
}

void SettingsStore::reset() {
  prefs_.clear();
}

AppSettings SettingsStore::load() const {
  AppSettings s;
  s.wifiSsid = prefs_.getString("ssid", "");
  s.wifiPass = prefs_.getString("pass", "");
  // Repair corrupt creds from older Web savePolicy bug (JSON null → "null").
  if (s.wifiSsid == "null" || s.wifiSsid == "undefined") {
    Serial.println("[settings] repair literal \"null\" SSID");
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
  // Clamp for runtime use only AFTER CRC — mutating before CRC used to false-fail
  // and wipe WiFi on upgrade / odd ahold values.
  uint16_t holdRaw = s.holdoverSec;
  if (s.holdoverSec < 10) {
    s.holdoverSec = 10;
  }
  if (s.holdoverSec > 600) {
    s.holdoverSec = 600;
  }
  s.autoReconnect = prefs_.getBool("arec", true);
  s.apPassword = prefs_.getString("appw", "");
  s.webPassword = prefs_.getString("webpw", "");
  if (s.apPassword == "null" || s.apPassword == "undefined") {
    s.apPassword = "";
  }
  if (s.webPassword == "null" || s.webPassword == "undefined") {
    s.webPassword = "";
  }

  const uint8_t aclm = static_cast<uint8_t>(prefs_.getUChar("aclm", 0));
  s.ntpAclMode = (aclm == static_cast<uint8_t>(NtpAclMode::AllowList)) ? NtpAclMode::AllowList
                                                                        : NtpAclMode::Off;
  s.ntpAclCount = 0;
  const uint8_t acln = static_cast<uint8_t>(prefs_.getUChar("acln", 0));
  char key[8];
  for (uint8_t i = 0; i < NTP_ACL_MAX_ENTRIES && i < acln; ++i) {
    snprintf(key, sizeof(key), "acl%u", static_cast<unsigned>(i));
    String dotted = prefs_.getString(key, "");
    IPAddress ip;
    if (!dotted.isEmpty() && ip.fromString(dotted) && static_cast<uint32_t>(ip) != 0) {
      s.ntpAcl[s.ntpAclCount++] = ip;
    }
  }

  s.tempComp = prefs_.getBool("tcmp", static_cast<bool>(CLK_TEMP_COMP_DEFAULT));
  s.tempCoeffCenti = static_cast<int16_t>(prefs_.getShort("tcpc", CLK_TEMP_COEFF_CENTI));
  if (s.tempCoeffCenti < -500) {
    s.tempCoeffCenti = -500;
  }
  if (s.tempCoeffCenti > 500) {
    s.tempCoeffCenti = 500;
  }

  // Display-only preference: not part of the CRC (missing key → default).
  s.oledIdleOffMs = prefs_.getULong("ooff", OLED_IDLE_OFF_DEFAULT_MS);

  const uint16_t ver = prefs_.getUShort("ver", 0);
  const uint32_t storedCrc = prefs_.getUInt("crc", 0);
  // CRC is integrity hint only. NEVER clear WiFi on mismatch — that forced SoftAP
  // after OTA when the CRC recipe or clamping changed while ver stayed the same.
  AppSettings forCrc = s;
  forCrc.holdoverSec = holdRaw;
  const uint32_t calc = settingsCrc(forCrc);
  bool needRewrite = false;
  if (ver != kSettingsVer) {
    needRewrite = !s.wifiSsid.isEmpty() || storedCrc != 0 || ver != 0;
    if (ver != 0) {
      Serial.printf("[settings] ver %u → %u — keep WiFi, refresh CRC\n",
                    static_cast<unsigned>(ver), static_cast<unsigned>(kSettingsVer));
    }
  } else if (storedCrc != 0 && calc != storedCrc) {
    needRewrite = true;
    Serial.printf("[settings] CRC mismatch stored=%08x calc=%08x — keep WiFi, refresh CRC\n",
                  static_cast<unsigned>(storedCrc), static_cast<unsigned>(calc));
  } else if (storedCrc == 0 && !s.wifiSsid.isEmpty()) {
    needRewrite = true;
  }
  if (needRewrite) {
    // Persist clamped holdover + current CRC so the next boot is clean.
    prefs_.putUShort("ahold", s.holdoverSec);
    prefs_.putUShort("ver", kSettingsVer);
    prefs_.putUInt("crc", settingsCrc(s));
  }

  if (s.wifiSsid.isEmpty()) {
    Serial.println("[settings] no saved WiFi SSID (NVS empty or never configured)");
  } else {
    Serial.printf("[settings] loaded WiFi ssid=\"%s\" static=%d arec=%d\n", s.wifiSsid.c_str(),
                  s.useStaticIp ? 1 : 0, s.autoReconnect ? 1 : 0);
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
  prefs_.putString("appw", s.apPassword);
  prefs_.putString("webpw", s.webPassword);
  prefs_.putUChar("aclm", static_cast<uint8_t>(s.ntpAclMode));
  prefs_.putUChar("acln", s.ntpAclCount);
  char key[8];
  for (uint8_t i = 0; i < NTP_ACL_MAX_ENTRIES; ++i) {
    snprintf(key, sizeof(key), "acl%u", static_cast<unsigned>(i));
    if (i < s.ntpAclCount) {
      prefs_.putString(key, s.ntpAcl[i].toString());
    } else {
      prefs_.remove(key);
    }
  }
  prefs_.putBool("tcmp", s.tempComp);
  prefs_.putShort("tcpc", s.tempCoeffCenti);
  prefs_.putULong("ooff", s.oledIdleOffMs);
  prefs_.putUShort("ver", kSettingsVer);
  prefs_.putUInt("crc", settingsCrc(s));
}
