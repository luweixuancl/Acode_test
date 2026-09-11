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

enum class NetWork : uint8_t {
  Idle = 0,
  Connecting = 1,
  Scanning = 2,
  Probing = 3,
};

static NetWork gNetWork = NetWork::Idle;
static AppSettings gPendingSta;
static bool gStopApOnConnectOk = false;
static bool gBootNeedApIfFail = true;
static bool gConnectFromAutoReconnect = false;

static bool startStaConnect(const AppSettings& settings, bool stopApOnOk) {
  if (gWifi.isBusy() || gNetWork != NetWork::Idle) {
    postUiText("WiFi busy");
    return false;
  }
  if (!gWifi.beginConnect(settings)) {
    postUiText("WiFi busy");
    return false;
  }
  gPendingSta = settings;
  gStopApOnConnectOk = stopApOnOk;
  gConnectFromAutoReconnect = false;
  gNetWork = NetWork::Connecting;
  postUiText("WiFi joining...");
  return true;
}

static void openSetupApIfNeeded(const char* uiMsg) {
  if (gWifi.isStaConnected() || gIpc.setupAp) {
    return;
  }
  gWifi.cancelAutoReconnect();
  gWifi.startSetupAp();
  gIpc.setupAp = true;
  postUiText(uiMsg != nullptr ? uiMsg : "AP setup mode");
}

static void finishConnect(WifiConnectState st) {
  gNetWork = NetWork::Idle;
  const bool fromAuto = gConnectFromAutoReconnect;
  gConnectFromAutoReconnect = false;

  if (st == WifiConnectState::Connected) {
    char buf[48];
    snprintf(buf, sizeof(buf), "OK %s", gWifi.localIp().toString().c_str());
    postUiText(buf);
    Serial.printf("STA IP: %s  (http://%s/)\n", gWifi.localIp().toString().c_str(),
                  gWifi.localIp().toString().c_str());
    // STA is up — SoftAP must go away (was escape hatch only).
    gWifi.stopAp();
    gIpc.setupAp = false;
    gBootNeedApIfFail = false;
  } else if (fromAuto) {
    // Backoff retry continues inside WifiManager; SoftAP only on give-up.
    postUiText("WiFi retry...");
  } else {
    postUiText("WiFi failed");
    // Boot: do not open SoftAP on the first 201 — schedule reconnect instead.
    if (gBootNeedApIfFail && !gPendingSta.wifiSsid.isEmpty()) {
      gWifi.armReconnect(gPendingSta, WIFI_RECONNECT_BACKOFF_1_MS);
      Serial.println("[wifi] boot join failed → scheduled reconnect (SoftAP after give-up)");
      postUiText("WiFi retry...");
    } else if (gBootNeedApIfFail) {
      openSetupApIfNeeded("AP setup mode");
      gBootNeedApIfFail = false;
    }
  }
  gStopApOnConnectOk = false;
}

static void handleConnect(const char* ssid, const char* pass) {
  if (!settingsLock(pdMS_TO_TICKS(500))) {
    postUiText("Settings busy");
    return;
  }
  gSettings.wifiSsid = ssid;
  gSettings.wifiPass = pass;
  AppSettings copy = gSettings;
  settingsUnlock();
  gStore.save(copy);

  startStaConnect(copy, gIpc.setupAp);
}

static void handleNetRequest(const NetRequest& req) {
  switch (req.type) {
    case NetReqType::ConnectWifi:
      handleConnect(req.ssid, req.pass);
      break;
    case NetReqType::ScanWifi: {
      if (gNetWork != NetWork::Idle || gWifi.isBusy()) {
        postUiText("WiFi busy");
        UiMsg msg;
        msg.type = UiMsgType::ScanFailed;
        msg.text[0] = '\0';
        xQueueSend(gIpc.uiMsg, &msg, 0);
        break;
      }
      if (!gWifi.startScan()) {
        UiMsg msg;
        msg.type = UiMsgType::ScanFailed;
        msg.text[0] = '\0';
        xQueueSend(gIpc.uiMsg, &msg, 0);
        break;
      }
      gNetWork = NetWork::Scanning;
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
      } else if (gNetWork != NetWork::Idle || gWifi.isBusy()) {
        postUiText("WiFi busy");
      } else if (!gWifi.isStaConnected()) {
        // No live STA to ARP-probe against; just try connect with static config.
        startStaConnect(copy, false);
      } else if (!gWifi.beginConflictProbe(copy.staticIp)) {
        postUiText("Probe failed");
      } else {
        gPendingSta = copy;
        gNetWork = NetWork::Probing;
        postUiText("Checking IP...");
      }
      break;
    }
    case NetReqType::UseDhcp: {
      if (!settingsLock(pdMS_TO_TICKS(200))) {
        postUiText("Settings busy");
        break;
      }
      gSettings.useStaticIp = false;
      AppSettings copy = gSettings;
      settingsUnlock();
      gStore.save(copy);
      if (!copy.wifiSsid.isEmpty()) {
        startStaConnect(copy, false);
      }
      break;
    }
    case NetReqType::StartWebSetup:
      gWifi.startSetupAp();
      gIpc.setupAp = true;
      break;
  }
}

static void pollDisconnectAndReconnect() {
  // Connecting owns DISC via pollConnect; elsewhere consume link-loss edges.
  if (gNetWork != NetWork::Connecting) {
    uint16_t discReason = 0;
    if (gWifi.consumeDisconnect(&discReason)) {
      char buf[40];
      snprintf(buf, sizeof(buf), "WiFi lost (%u)", discReason);
      postUiText(buf);
    }
  }

  if (gWifi.consumeReconnectGiveUp()) {
    if (gBootNeedApIfFail || !gIpc.setupAp) {
      openSetupApIfNeeded("AP setup mode");
      gBootNeedApIfFail = false;
    }
  }

  if (gNetWork != NetWork::Idle) {
    return;
  }

  AppSettings recon;
  if (gWifi.pollAutoReconnect(&recon)) {
    gPendingSta = recon;
    gStopApOnConnectOk = gIpc.setupAp;
    gConnectFromAutoReconnect = true;
    gNetWork = NetWork::Connecting;
    postUiText("WiFi rejoin...");
  }
}

static void pollNetWork() {
  pollDisconnectAndReconnect();

  switch (gNetWork) {
    case NetWork::Connecting: {
      const WifiConnectState st = gWifi.pollConnect();
      if (st == WifiConnectState::Connecting) {
        break;
      }
      finishConnect(st);
      break;
    }
    case NetWork::Scanning: {
      std::vector<WifiNetwork> nets;
      const WifiScanState st = gWifi.pollScan(&nets);
      if (st == WifiScanState::Running) {
        break;
      }
      gNetWork = NetWork::Idle;
      if (st == WifiScanState::Done) {
        if (xSemaphoreTake(gIpc.scanMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          gIpc.scanResults = nets;
          gIpc.scanReady = true;
          xSemaphoreGive(gIpc.scanMutex);
        }
        UiMsg msg;
        msg.type = nets.empty() ? UiMsgType::ScanFailed : UiMsgType::ScanResult;
        msg.text[0] = '\0';
        xQueueSend(gIpc.uiMsg, &msg, 0);
      } else {
        UiMsg msg;
        msg.type = UiMsgType::ScanFailed;
        msg.text[0] = '\0';
        xQueueSend(gIpc.uiMsg, &msg, 0);
      }
      break;
    }
    case NetWork::Probing: {
      const WifiProbeState st = gWifi.pollConflictProbe();
      if (st == WifiProbeState::Running) {
        break;
      }
      gNetWork = NetWork::Idle;
      if (st == WifiProbeState::Conflict) {
        postUiText("IP CONFLICT!");
        Serial.printf("IP conflict on %s\n", gPendingSta.staticIp.toString().c_str());
      } else if (st == WifiProbeState::Clear) {
        startStaConnect(gPendingSta, false);
      } else {
        postUiText("Probe failed");
      }
      break;
    }
    case NetWork::Idle:
    default:
      break;
  }
}

static void taskTime(void* /*arg*/) {
  esp_task_wdt_add(nullptr);
  gGps.setTimeTask(xTaskGetCurrentTaskHandle());

  // Cache last successful settings read so a busy mutex never looks like Refuse
  // and aborts Holdover mid-flight.
  AnomalyPolicy cachedPolicy = AnomalyPolicy::Refuse;
  uint16_t cachedHoldSec = CLK_HOLDOVER_SHORT_SEC;
  if (settingsLock(pdMS_TO_TICKS(100))) {
    cachedPolicy = gSettings.anomalyPolicy;
    cachedHoldSec = gSettings.holdoverSec;
    settingsUnlock();
  }

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));

    if (settingsLock(0)) {
      cachedPolicy = gSettings.anomalyPolicy;
      cachedHoldSec = gSettings.holdoverSec;
      settingsUnlock();
    }

    gGps.loop(cachedPolicy, cachedHoldSec);
    gNtp.loop(gGps);
    ipcKickTime();
    esp_task_wdt_reset();
  }
}

static void taskNet(void* /*arg*/) {
  AppSettings boot;
  if (settingsLock(pdMS_TO_TICKS(500))) {
    boot = gSettings;
    settingsUnlock();
  }

  gWifi.setAutoReconnect(boot.autoReconnect);

  // Start HTTP early so SoftAP/STA pages stay responsive during connect.
  gPortal.begin(&gWifi, &gGps, &gNtp);

  if (!boot.wifiSsid.isEmpty()) {
    gBootNeedApIfFail = true;
    if (!gWifi.beginConnect(boot)) {
      gWifi.startSetupAp();
      gIpc.setupAp = true;
      postUiText("AP setup mode");
      gBootNeedApIfFail = false;
    } else {
      gPendingSta = boot;
      gNetWork = NetWork::Connecting;
      postUiText("WiFi joining...");
    }
  } else {
    gWifi.startSetupAp();
    gIpc.setupAp = true;
    postUiText("AP setup mode");
    gBootNeedApIfFail = false;
  }

  for (;;) {
    NetRequest req;
    while (xQueueReceive(gIpc.netReq, &req, 0) == pdTRUE) {
      handleNetRequest(req);
    }

    String ssid, pass;
    if (gPortal.consumeConnectRequest(ssid, pass)) {
      handleConnect(ssid.c_str(), pass.c_str());
    }

    pollNetWork();
    gPortal.loop();
    ipcKickNet();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void taskUi(void* /*arg*/) {
  for (;;) {
    gEnc.loop();
    const GpsStatus st = gGps.snapshot();
    ipcKickUi();
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
