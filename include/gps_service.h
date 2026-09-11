#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "local_clock.h"
#include "settings.h"

struct GpsStatus {
  bool validFix = false;
  uint8_t satellites = 0;
  double lat = 0;
  double lon = 0;
  float hdop = 99.9f;
  uint32_t utcEpoch = 0;
  uint32_t ageMs = 0xFFFFFFFF;
  bool ppsSeen = false;
  uint32_t ppsCount = 0;
  bool ppsFresh = false;
  uint32_t qualityMs = 0xFFFFFFFF;
  bool timeValid = false;
  ClockState clockState = ClockState::Acquiring;
  int32_t residualMs = 0;
  float freqPpm = 0;
  uint32_t holdoverMs = 0;
};

class GpsService {
 public:
  void begin();
  // Call only from task-time.
  void loop(AnomalyPolicy policy, uint16_t holdoverSec);

  GpsStatus snapshot() const;

  bool ppsFresh() const;
  bool nowUtc(uint32_t& seconds, uint32_t& fraction) const;
  uint32_t qualityMs() const;
  const LocalClock& localClock() const { return localClock_; }

  void setTimeTask(TaskHandle_t handle) { timeTask_ = handle; }

 private:
  static void IRAM_ATTR onPpsIsr();
  void parseNmea();
  void commitNmeaTime(uint32_t epochSec, AnomalyPolicy policy, uint16_t holdoverSec);
  void publishStatus(const GpsStatus& work);

  HardwareSerial gpsSerial_{GPS_UART_NUM};
  TinyGPSPlus gps_;
  GpsStatus published_;
  LocalClock localClock_;
  uint32_t lastDebugMs_ = 0;

  uint32_t commitEpoch_ = 0;
  uint32_t commitPpsCount_ = 0;
  uint32_t commitMs_ = 0;
  uint32_t lastCommittedSecond_ = 0xFFFFFFFF;
  bool haveCommit_ = false;
  bool ppsSeen_ = false;

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  static portMUX_TYPE ppsMux_;
  static volatile uint32_t ppsMillis_;
  static volatile uint32_t ppsCount_;
  static volatile uint64_t ppsEdgeUs_;
  static volatile bool ppsFlag_;
  static TaskHandle_t timeTask_;
};
