#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

void WifiManager::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp32c3-ntp");
}

bool WifiManager::connectSta(const AppSettings& settings) {
  if (settings.wifiSsid.isEmpty()) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  if (settings.useStaticIp) {
    if (!WiFi.config(settings.staticIp, settings.gateway, settings.subnet, settings.dns)) {
      Serial.println("WiFi.config failed");
    }
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void WifiManager::startSetupAp() {
  String ssid = String(AP_SSID_PREFIX) + "-" + String((uint32_t)ESP.getEfuseMac() & 0xFFFF, HEX);
  ssid.toUpperCase();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid.c_str(), AP_PASSWORD);
  Serial.printf("Setup AP: %s / %s  IP=%s\n", ssid.c_str(), AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
}

void WifiManager::stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

bool WifiManager::isStaConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

IPAddress WifiManager::localIp() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP();
  }
  return WiFi.softAPIP();
}

String WifiManager::macAddress() const {
  return WiFi.macAddress();
}

std::vector<WifiNetwork> WifiManager::scanNetworks() {
  std::vector<WifiNetwork> out;
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  for (int i = 0; i < n; ++i) {
    WifiNetwork net;
    net.ssid = WiFi.SSID(i);
    net.rssi = WiFi.RSSI(i);
    net.enc = WiFi.encryptionType(i);
    if (net.ssid.length() > 0) {
      out.push_back(net);
    }
  }
  WiFi.scanDelete();
  return out;
}

bool WifiManager::pingProbe(const IPAddress& ip) {
  ip4_addr_t addr;
  IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);

  struct netif* nif = netif_default;
  if (nif == nullptr) {
    return false;
  }

  etharp_cleanup_netif(nif);
  err_t err = etharp_request(nif, &addr);
  if (err != ERR_OK) {
    return false;
  }

  uint32_t start = millis();
  while (millis() - start < IP_CONFLICT_TIMEOUT_MS) {
    delay(20);
    struct eth_addr* eth_ret = nullptr;
    const ip4_addr_t* ip_ret = nullptr;
    if (etharp_find_addr(nif, &addr, &eth_ret, &ip_ret) >= 0) {
      return true;
    }
  }
  return false;
}

bool WifiManager::detectIpConflict(const IPAddress& ip) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (ip == WiFi.localIP()) {
    return false;
  }
  return pingProbe(ip);
}

bool WifiManager::applyStaticIp(const AppSettings& settings) {
  if (!settings.useStaticIp) {
    return true;
  }
  if (detectIpConflict(settings.staticIp)) {
    return false;
  }
  return connectSta(settings);
}
