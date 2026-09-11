#pragma once

#include "gps_service.h"

// 合宙 CORE ESP32 on-board LEDs: D4=GPIO12, D5=GPIO13, active HIGH.
class StatusLeds {
 public:
  void begin();
  // Prefer passing a snapshot so UI task never races GPS writers.
  void loop(bool apMode, bool wifiStaOk, const GpsStatus& st);

 private:
  static void writeBlink(uint8_t pin, uint32_t nowMs, uint32_t halfPeriodMs);
  static void writeHeartbeat(uint8_t pin, uint32_t nowMs);
  static bool tasksStale(uint32_t nowMs);

  uint32_t lastUpdateMs_ = 0;
};
