#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "gps_service.h"

class NtpServer {
 public:
  void begin();
  void loop(const GpsService& gps);

 private:
  void handlePacket(const GpsService& gps);
  static void writeTimestamp(uint8_t* pkt, int offset, uint32_t sec, uint32_t frac);

  WiFiUDP udp_;
  uint8_t packet_[48];
};
