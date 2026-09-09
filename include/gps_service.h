#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include "config.h"

struct GpsStatus {
  bool validFix = false;
  uint8_t satellites = 0;
  double lat = 0;
  double lon = 0;
  float hdop = 99.9f;
  uint32_t utcEpoch = 0;      // Unix seconds (UTC)
  uint32_t ageMs = 0xFFFFFFFF;
  bool ppsSeen = false;
  uint32_t ppsCount = 0;
};

class GpsService {
 public:
  void begin();
  void loop();
  const GpsStatus& status() const { return status_; }
  // Best-effort current UTC unix time using last RMC + millis since PPS.
  bool nowUtc(uint32_t& seconds, uint32_t& fraction) const;

 private:
  static void IRAM_ATTR onPpsIsr();
  void parseNmea();

  HardwareSerial gpsSerial_{GPS_UART_NUM};
  TinyGPSPlus gps_;
  GpsStatus status_;

  static volatile uint32_t ppsMillis_;
  static volatile uint32_t ppsCount_;
  static volatile bool ppsFlag_;
};
