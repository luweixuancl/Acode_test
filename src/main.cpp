#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_ipc.h"
#include "config.h"
#include "settings.h"
#include "gps_service.h"
#include "ntp_server.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "encoder.h"
#include "display_ui.h"
#include "status_leds.h"

SettingsStore gStore;
AppSettings gSettings;
GpsService gGps;
NtpServer gNtp;
WifiManager gWifi;
WebPortal gPortal;
EncoderInput gEnc;
DisplayUi gUi;
StatusLeds gLeds;

TaskHandle_t gTaskTime = nullptr;
TaskHandle_t gTaskNet = nullptr;
TaskHandle_t gTaskUi = nullptr;

static void handleConnect(const char* ssid, const char* pass) {
  if (!settingsLock(pdMS_TO_TICKS(500))) {
    postUiText("Settings busy");
    return;
  }
  gSettings.wifiSsid = ssid;
  gSettings.wifiPass = pass;
  gStore.save(gSettings);
  AppSettings copy = gSettings;
  settingsUnlock();

  postUiText("WiFi joining...");
  const bool ok = gWifi.connectSta(copy);
  if (ok) {
    char buf[48];
    snprintf(buf, sizeof(buf), "OK %s", gWifi.localIp().toString().c_str());
    postUiText(buf);
    Serial.printf("STA IP: %s  (http://%s/)\n", gWifi.localIp().toString().c_str(),
                  gWifi.localIp().toString().c_str());
    if (gIpc.setupAp) {
      gWifi.stopAp();
      gIpc.setupAp = false;
    }
  } else {
    postUiText("WiFi failed");
  }
}

static void handleNetRequest(const NetRequest& req) {
  switch (req.type) {
    case NetReqType::ConnectWifi:
      handleConnect(req.ssid, req.pass);
      break;
    case NetReqType::ScanWifi: {
      auto nets = gWifi.scanNetworks();
      if (xSemaphoreTake(gIpc.scanMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        gIpc.scanResults = nets;
        gIpc.scanReady = true;
        xSemaphoreGive(gIpc.scanMutex);
      }
      UiMsg msg;
      msg.type = nets.empty() ? UiMsgType::ScanFailed : UiMsgType::ScanResult;
      msg.text[0] = '\0';
      xQueueSend(gIpc.uiMsg, &msg, 0);
      break;
    }
    case NetReqType::ApplyStaticIp: {
      if (!settingsLock(pdMS_TO_TICKS(200))) {
        postUiText("Settings busy");
        break;
      }
      AppSettings copy = gSettings;
      settingsUnlock();
      if (!gWifi.isStaConnected() && copy.wifiSsid.isEmpty()) {
        postUiText("Connect WiFi first");
      } else if (gWifi.detectIpConflict(copy.staticIp)) {
        postUiText("IP CONFLICT!");
        Serial.printf("IP conflict on %s\n", copy.staticIp.toString().c_str());
      } else {
        const bool ok = gWifi.connectSta(copy);
        if (ok) {
          char buf[40];
          snprintf(buf, sizeof(buf), "IP %s", gWifi.localIp().toString().c_str());
          postUiText(buf);
        } else {
          postUiText("Static IP fail");
        }
      }
      break;
    }
    case NetReqType::UseDhcp: {
      if (settingsLock(pdMS_TO_TICKS(200))) {
        gSettings.useStaticIp = false;
        gStore.save(gSettings);
        AppSettings copy = gSettings;
        settingsUnlock();
        if (!copy.wifiSsid.isEmpty()) {
          gWifi.connectSta(copy);
        }
      }
      break;
    }
    case NetReqType::StartWebSetup:
      gWifi.startSetupAp();
      gIpc.setupAp = true;
      break;
  }
}

static void taskTime(void* /*arg*/) {
  esp_task_wdt_add(nullptr);
  gGps.setTimeTask(xTaskGetCurrentTaskHandle());

  for (;;) {
    // Wake on PPS notify, or poll every 1ms for NMEA/UDP.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    gGps.loop();
    gNtp.loop(gGps);
    esp_task_wdt_reset();
  }
}

static void taskNet(void* /*arg*/) {
  // Initial STA connect (may block — isolated from time task).
  AppSettings boot;
  if (settingsLock(pdMS_TO_TICKS(500))) {
    boot = gSettings;
    settingsUnlock();
  }
  if (!boot.wifiSsid.isEmpty()) {
    if (gWifi.connectSta(boot)) {
      Serial.printf("STA IP: %s  (http://%s/)\n", gWifi.localIp().toString().c_str(),
                    gWifi.localIp().toString().c_str());
    }
  }
  if (!gWifi.isStaConnected()) {
    gWifi.startSetupAp();
    gIpc.setupAp = true;
    postUiText("AP setup mode");
  }

  gPortal.begin(&gWifi, &gGps, &gNtp);

  for (;;) {
    NetRequest req;
    while (xQueueReceive(gIpc.netReq, &req, 0) == pdTRUE) {
      handleNetRequest(req);
    }

    String ssid, pass;
    if (gPortal.consumeConnectRequest(ssid, pass)) {
      handleConnect(ssid.c_str(), pass.c_str());
    }

    gPortal.loop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void taskUi(void* /*arg*/) {
  for (;;) {
    gEnc.loop();
    const GpsStatus st = gGps.snapshot();
    gLeds.loop(gIpc.setupAp, gWifi.isStaConnected(), st);
    gUi.loop(gEnc, gGps, gWifi);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32-C3 GNSS NTP Server (RTOS)");

  if (!ipcInit()) {
    Serial.println("IPC init failed");
  }

  gStore.begin();
  gSettings = gStore.load();

  gEnc.begin();
  gLeds.begin();
  gUi.begin();
  gGps.begin();
  gWifi.begin();
  gNtp.begin();

  // Priority: time=5 > net=2 > ui=1 (all below WiFi/lwIP ~18+)
  xTaskCreatePinnedToCore(taskTime, "task-time", 6144, nullptr, 5, &gTaskTime, 0);
  xTaskCreatePinnedToCore(taskNet, "task-net", 8192, nullptr, 2, &gTaskNet, 0);
  xTaskCreatePinnedToCore(taskUi, "task-ui", 4096, nullptr, 1, &gTaskUi, 0);

  Serial.println("Tasks started: time=5 net=2 ui=1");
}

void loop() {
  // Arduino loopTask is unused after RTOS refactor.
  vTaskDelete(nullptr);
}
