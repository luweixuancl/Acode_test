#pragma once

#include "gps_service.h"

// D4 (GPIO12) and D5 (GPIO13), active HIGH.
// D4: device / network. D5: GNSS time quality.
class StatusLeds {
 public:
  void begin();
  void loop(bool apMode, bool wifiStaOk, const GpsService& gps);

 private:
  static void writeBlink(uint8_t pin, uint32_t nowMs, uint32_t halfPeriodMs);

  uint32_t lastUpdateMs_ = 0;
};
