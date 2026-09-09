#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "settings.h"
#include "wifi_manager.h"

class WebPortal {
 public:
  void begin(WifiManager* wifi, SettingsStore* store, AppSettings* settings);
  void loop();
  bool consumeConnectRequest(String& ssid, String& pass);

 private:
  void handleRoot();
  void handleScan();
  void handleSave();
  void handleStatus();
  String buildPage(const String& body) const;

  WebServer server_{80};
  WifiManager* wifi_ = nullptr;
  SettingsStore* store_ = nullptr;
  AppSettings* settings_ = nullptr;
  bool pendingConnect_ = false;
  bool started_ = false;
  String pendingSsid_;
  String pendingPass_;
};
