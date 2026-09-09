#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"
#include "gps_service.h"
#include "ntp_server.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "encoder.h"
#include "display_ui.h"

SettingsStore gStore;
AppSettings gSettings;
GpsService gGps;
NtpServer gNtp;
WifiManager gWifi;
WebPortal gPortal;
EncoderInput gEnc;
DisplayUi gUi;

bool gWebPortalActive = false;

void connectFromCredentials(const String& ssid, const String& pass) {
  gSettings.wifiSsid = ssid;
  gSettings.wifiPass = pass;
  gStore.save(gSettings);
  gUi.showMessage("WiFi joining...");
  bool ok = gWifi.connectSta(gSettings);
  if (ok) {
    gUi.showMessage(String("OK ") + gWifi.localIp().toString());
    if (gWebPortalActive) {
      // Keep AP briefly then drop to STA-only
      delay(500);
      gWifi.stopAp();
      gWebPortalActive = false;
    }
  } else {
    gUi.showMessage("WiFi failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32-C3 GNSS NTP Server");

  gStore.begin();
  gSettings = gStore.load();

  gEnc.begin();
  gUi.begin();
  gGps.begin();
  gWifi.begin(gSettings);
  gNtp.begin();

  if (!gWifi.isStaConnected()) {
    // Auto-start setup AP when no WiFi yet
    gWifi.startSetupAp();
    gPortal.begin(&gWifi, &gStore, &gSettings);
    gWebPortalActive = true;
    gUi.showMessage("AP setup mode");
  } else {
    Serial.printf("STA IP: %s\n", gWifi.localIp().toString().c_str());
  }
}

void loop() {
  gGps.loop();
  gEnc.loop();
  gNtp.loop(gGps);
  gUi.loop(gEnc, gGps, gWifi, gSettings, gStore);

  if (gWebPortalActive) {
    gPortal.loop();
    String ssid, pass;
    if (gPortal.consumeConnectRequest(ssid, pass)) {
      connectFromCredentials(ssid, pass);
    }
  }

  String ssid, pass;
  if (gUi.takePendingWifi(ssid, pass)) {
    connectFromCredentials(ssid, pass);
  }

  if (gUi.takeStartWebSetup()) {
    gWifi.startSetupAp();
    if (!gWebPortalActive) {
      gPortal.begin(&gWifi, &gStore, &gSettings);
      gWebPortalActive = true;
    }
  }

  if (gUi.takeUseDhcp()) {
    gSettings.useStaticIp = false;
    gStore.save(gSettings);
    if (!gSettings.wifiSsid.isEmpty()) {
      gWifi.connectSta(gSettings);
    }
  }

  if (gUi.takeApplyStaticIp()) {
    if (!gWifi.isStaConnected() && gSettings.wifiSsid.isEmpty()) {
      gUi.showMessage("Connect WiFi first");
    } else if (gWifi.detectIpConflict(gSettings.staticIp)) {
      gUi.showMessage("IP CONFLICT!");
      Serial.printf("IP conflict on %s\n", gSettings.staticIp.toString().c_str());
    } else {
      gSettings.useStaticIp = true;
      gStore.save(gSettings);
      bool ok = gWifi.connectSta(gSettings);
      if (ok) {
        gUi.showMessage(String("IP ") + gWifi.localIp().toString());
      } else {
        gUi.showMessage("Static IP fail");
      }
    }
  }
}
