#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "settings.h"
#include "wifi_manager.h"

class GpsService;
class NtpServer;

class WebPortal {
 public:
  void begin(WifiManager* wifi, GpsService* gps, NtpServer* ntp);
  void loop();
  bool consumeConnectRequest(String& ssid, String& pass);

 private:
  void handleRoot();
  void handleSetup();
  void handleScan();
  void handleSave();
  void handleStatus();
  String buildPage(const String& title, const String& body, bool refresh = false) const;
  String formatUtc(uint32_t epoch, int8_t tzHours) const;

  WebServer server_{80};
  WifiManager* wifi_ = nullptr;
  GpsService* gps_ = nullptr;
  NtpServer* ntp_ = nullptr;
  bool pendingConnect_ = false;
  bool started_ = false;
  String pendingSsid_;
  String pendingPass_;
};
