#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "settings.h"
#include "wifi_manager.h"

// Cross-task IPC for RTOS refactor (time / net / ui).

enum class NetReqType : uint8_t {
  ConnectWifi,
  ScanWifi,
  ApplyStaticIp,
  UseDhcp,
  StartWebSetup,
};

struct NetRequest {
  NetReqType type = NetReqType::ConnectWifi;
  char ssid[33] = {};
  char pass[65] = {};
  IPAddress staticIp;
};

enum class UiMsgType : uint8_t {
  Text,
  ScanResult,
  ScanFailed,
};

struct UiMsg {
  UiMsgType type = UiMsgType::Text;
  char text[48] = {};
  // Scan results: payload lives in shared scan buffer guarded by scanMutex.
};

struct AppIpc {
  QueueHandle_t netReq = nullptr;
  QueueHandle_t uiMsg = nullptr;
  SemaphoreHandle_t settingsMutex = nullptr;
  SemaphoreHandle_t scanMutex = nullptr;
  std::vector<WifiNetwork> scanResults;
  bool scanReady = false;
  bool setupAp = false;
};

extern AppIpc gIpc;
extern AppSettings gSettings;
extern SettingsStore gStore;

bool ipcInit();
bool settingsLock(TickType_t ticks = portMAX_DELAY);
void settingsUnlock();
bool postNetRequest(const NetRequest& req);
bool postUiText(const char* text);
