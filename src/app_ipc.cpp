#include "app_ipc.h"
#include <cstring>

AppIpc gIpc;

bool ipcInit() {
  gIpc.netReq = xQueueCreate(8, sizeof(NetRequest));
  gIpc.uiMsg = xQueueCreate(8, sizeof(UiMsg));
  gIpc.settingsMutex = xSemaphoreCreateMutex();
  gIpc.scanMutex = xSemaphoreCreateMutex();
  return gIpc.netReq && gIpc.uiMsg && gIpc.settingsMutex && gIpc.scanMutex;
}

bool settingsLock(TickType_t ticks) {
  return xSemaphoreTake(gIpc.settingsMutex, ticks) == pdTRUE;
}

void settingsUnlock() {
  xSemaphoreGive(gIpc.settingsMutex);
}

bool postNetRequest(const NetRequest& req) {
  if (!gIpc.netReq) {
    return false;
  }
  return xQueueSend(gIpc.netReq, &req, 0) == pdTRUE;
}

bool postUiText(const char* text) {
  if (!gIpc.uiMsg) {
    return false;
  }
  UiMsg msg;
  msg.type = UiMsgType::Text;
  strncpy(msg.text, text ? text : "", sizeof(msg.text) - 1);
  msg.text[sizeof(msg.text) - 1] = '\0';
  return xQueueSend(gIpc.uiMsg, &msg, 0) == pdTRUE;
}
