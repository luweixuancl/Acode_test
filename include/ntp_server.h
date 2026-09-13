#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"
#include "gps_service.h"

class NtpServer {
 public:
  void begin();
  // Call only from task-time.
  void loop(const GpsService& gps);

  uint32_t requestCount() const { return requestCount_; }
  uint32_t servedCount() const { return servedCount_; }
  uint32_t rateLimitedCount() const { return rateLimitedCount_; }
  uint32_t deniedCount() const { return deniedCount_; }
  uint32_t droppedCount() const { return droppedCount_; }
  uint8_t activeClientCount() const;

 private:
  enum class Admit : uint8_t { Allow = 0, Rate = 1, Deny = 2, Drop = 3 };

  struct ClientSlot {
    uint32_t ip = 0;
    uint32_t windowStartMs = 0;
    uint16_t windowCount = 0;
    uint32_t overSinceMs = 0;   // first time this window exceeded rate; 0 = ok
    uint32_t denyUntilMs = 0;   // 0 = not in DENY cooldown
    uint32_t lastSeenMs = 0;
  };

  void handlePacket(const GpsService& gps);
  Admit admitClient(uint32_t ip, uint32_t nowMs);
  bool admitGlobal(uint32_t nowMs);
  int findClientSlot(uint32_t ip, uint32_t nowMs);
  void sendKiss(const char kiss[4], uint8_t vn, uint8_t poll);
  void sendNormal(const GpsService& gps, bool haveTime, uint32_t recvSec, uint32_t recvFrac);
  static void writeTimestamp(uint8_t* pkt, int offset, uint32_t sec, uint32_t frac);
  static void writeU32(uint8_t* pkt, int offset, uint32_t v);
  static void writeRefId(uint8_t* pkt, const char id[4]);

  WiFiUDP udp_;
  uint8_t packet_[48];
  ClientSlot clients_[NTP_CLIENT_SLOTS];

  uint32_t requestCount_ = 0;
  uint32_t servedCount_ = 0;
  uint32_t rateLimitedCount_ = 0;
  uint32_t deniedCount_ = 0;
  uint32_t droppedCount_ = 0;

  uint32_t globalWindowStartMs_ = 0;
  uint16_t globalWindowCount_ = 0;
};
