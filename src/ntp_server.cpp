#include "ntp_server.h"
#include "config.h"
#include <WiFi.h>
#include <cstring>

void NtpServer::begin() {
  udp_.begin(NTP_UDP_PORT);
  memset(clients_, 0, sizeof(clients_));
}

void NtpServer::writeTimestamp(uint8_t* pkt, int offset, uint32_t sec, uint32_t frac) {
  writeU32(pkt, offset, sec);
  writeU32(pkt, offset + 4, frac);
}

void NtpServer::writeU32(uint8_t* pkt, int offset, uint32_t v) {
  pkt[offset + 0] = (v >> 24) & 0xFF;
  pkt[offset + 1] = (v >> 16) & 0xFF;
  pkt[offset + 2] = (v >> 8) & 0xFF;
  pkt[offset + 3] = v & 0xFF;
}

void NtpServer::writeRefId(uint8_t* pkt, const char id[4]) {
  pkt[12] = static_cast<uint8_t>(id[0]);
  pkt[13] = static_cast<uint8_t>(id[1]);
  pkt[14] = static_cast<uint8_t>(id[2]);
  pkt[15] = static_cast<uint8_t>(id[3]);
}

uint8_t NtpServer::activeClientCount() const {
  uint8_t n = 0;
  const uint32_t now = millis();
  for (uint8_t i = 0; i < NTP_CLIENT_SLOTS; ++i) {
    if (clients_[i].ip != 0 && (now - clients_[i].lastSeenMs) < 60000UL) {
      n++;
    }
  }
  return n;
}

int NtpServer::findClientSlot(uint32_t ip, uint32_t nowMs) {
  int freeIdx = -1;
  int lruIdx = 0;
  uint32_t lruSeen = UINT32_MAX;
  for (int i = 0; i < NTP_CLIENT_SLOTS; ++i) {
    if (clients_[i].ip == ip) {
      return i;
    }
    if (clients_[i].ip == 0 && freeIdx < 0) {
      freeIdx = i;
    }
    if (clients_[i].lastSeenMs <= lruSeen) {
      lruSeen = clients_[i].lastSeenMs;
      lruIdx = i;
    }
  }
  const int idx = freeIdx >= 0 ? freeIdx : lruIdx;
  clients_[idx] = ClientSlot{};
  clients_[idx].ip = ip;
  clients_[idx].windowStartMs = nowMs;
  clients_[idx].lastSeenMs = nowMs;
  return idx;
}

bool NtpServer::admitGlobal(uint32_t nowMs) {
  if (globalWindowStartMs_ == 0 || (nowMs - globalWindowStartMs_) >= NTP_RATE_WINDOW_MS) {
    globalWindowStartMs_ = nowMs;
    globalWindowCount_ = 0;
  }
  if (globalWindowCount_ >= NTP_GLOBAL_RATE_PER_SEC) {
    return false;
  }
  globalWindowCount_++;
  return true;
}

NtpServer::Admit NtpServer::admitClient(uint32_t ip, uint32_t nowMs) {
  ClientSlot& c = clients_[findClientSlot(ip, nowMs)];
  c.lastSeenMs = nowMs;

  if (c.denyUntilMs != 0) {
    if (static_cast<int32_t>(nowMs - c.denyUntilMs) < 0) {
      // Already in DENY cooldown — silent drop (avoid KoD flood).
      return Admit::Drop;
    }
    c.denyUntilMs = 0;
    c.overSinceMs = 0;
    c.windowStartMs = nowMs;
    c.windowCount = 0;
  }

  if (c.windowStartMs == 0 || (nowMs - c.windowStartMs) >= NTP_RATE_WINDOW_MS) {
    // Only clear sustained-over timer after a compliant window (client backed off).
    if (c.windowCount <= NTP_RATE_PER_IP_PER_SEC) {
      c.overSinceMs = 0;
    }
    c.windowStartMs = nowMs;
    c.windowCount = 0;
  }

  c.windowCount++;
  if (c.windowCount <= NTP_RATE_PER_IP_PER_SEC) {
    return Admit::Allow;
  }

  // Over per-IP rate.
  if (c.overSinceMs == 0) {
    c.overSinceMs = nowMs;
  }
  if ((nowMs - c.overSinceMs) >= NTP_RATE_TO_DENY_MS) {
    c.denyUntilMs = nowMs + NTP_DENY_COOLDOWN_MS;
    c.overSinceMs = 0;
    return Admit::Deny;  // one DENY kiss, then Drop until cooldown ends
  }
  return Admit::Rate;
}

void NtpServer::sendKiss(const char kiss[4], uint8_t vn, uint8_t poll) {
  // Preserve client Transmit as Originate before we rewrite the packet.
  uint8_t originate[8];
  memcpy(originate, packet_ + 40, 8);

  memset(packet_, 0, 48);
  if (vn < 1 || vn > 4) {
    vn = 3;
  }
  if (poll < 4 || poll > 17) {
    poll = 4;
  }
  // LI=3 (unsync), stratum 0 kiss, server mode 4
  packet_[0] = static_cast<uint8_t>((3 << 6) | (vn << 3) | 4);
  packet_[1] = 0;
  packet_[2] = poll;
  packet_[3] = static_cast<uint8_t>(-6);
  writeRefId(packet_, kiss);
  memcpy(packet_ + 24, originate, 8);

  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(packet_, 48);
  udp_.endPacket();
}

void NtpServer::sendNormal(const GpsService& gps, bool haveTime, uint32_t recvSec, uint32_t recvFrac) {
  const bool ppsOk = gps.ppsFresh();
  const uint32_t qMs = gps.qualityMs();
  const ClockState clk = gps.snapshot().clockState;

  const bool syncOk =
      haveTime && (clk == ClockState::Locked || clk == ClockState::Degraded ||
                   clk == ClockState::Holdover);
  uint32_t ntpSec = syncOk ? (recvSec + NTP_EPOCH_DELTA) : 0;
  uint32_t ntpFrac = syncOk ? recvFrac : 0;
  uint8_t li = syncOk ? 0 : 3;
  uint8_t vn = (packet_[0] >> 3) & 0x07;
  if (vn < 1 || vn > 4) {
    vn = 3;
  }
  packet_[0] = static_cast<uint8_t>((li << 6) | (vn << 3) | 4);
  packet_[1] = syncOk ? 1 : 16;
  uint8_t poll = packet_[2];
  if (poll < 4 || poll > 17) {
    poll = 4;
  }
  packet_[2] = poll;
  packet_[3] = syncOk ? (ppsOk ? static_cast<uint8_t>(-10) : static_cast<uint8_t>(-6))
                       : static_cast<uint8_t>(-6);

  memset(packet_ + 4, 0, 4);

  uint32_t dispersion = 0;
  if (!syncOk || qMs == 0xFFFFFFFF) {
    dispersion = 0xFFFF0000UL;
  } else {
    uint64_t d = (static_cast<uint64_t>(qMs) * 65536ULL) / 1000ULL;
    if (d > 0xFFFFFFFFULL) {
      d = 0xFFFFFFFFULL;
    }
    dispersion = static_cast<uint32_t>(d);
  }
  writeU32(packet_, 8, dispersion);

  if (syncOk) {
    writeRefId(packet_, "GPSS");
  } else {
    writeRefId(packet_, "INIT");
  }

  uint32_t refSec = 0;
  uint32_t refFrac = 0;
  if (syncOk && gps.referenceUtc(refSec, refFrac)) {
    writeTimestamp(packet_, 16, refSec + NTP_EPOCH_DELTA, refFrac);
  } else {
    writeTimestamp(packet_, 16, ntpSec, syncOk ? ntpFrac : 0);
  }

  memcpy(packet_ + 24, packet_ + 40, 8);
  writeTimestamp(packet_, 32, ntpSec, ntpFrac);

  uint32_t txSec = 0;
  uint32_t txFrac = 0;
  if (syncOk) {
    if (!gps.nowUtc(txSec, txFrac)) {
      txSec = recvSec;
      txFrac = recvFrac;
    }
    writeTimestamp(packet_, 40, txSec + NTP_EPOCH_DELTA, txFrac);
  } else {
    writeTimestamp(packet_, 40, 0, 0);
  }

  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(packet_, 48);
  udp_.endPacket();
}

void NtpServer::loop(const GpsService& gps) {
  for (int i = 0; i < NTP_MAX_PACKETS_PER_LOOP; ++i) {
    if (!udp_.parsePacket()) {
      break;
    }
    handlePacket(gps);
  }
}

void NtpServer::handlePacket(const GpsService& gps) {
  uint32_t recvSec = 0;
  uint32_t recvFrac = 0;
  const bool haveTime = gps.nowUtc(recvSec, recvFrac);

  const int len = udp_.read(packet_, sizeof(packet_));
  if (len < 48) {
    droppedCount_++;
    return;
  }
  requestCount_++;

  const uint32_t nowMs = millis();
  const uint32_t ip = static_cast<uint32_t>(udp_.remoteIP());
  const uint8_t vn = (packet_[0] >> 3) & 0x07;
  const uint8_t poll = packet_[2];

  if (!admitGlobal(nowMs)) {
    // Global flood: silent drop to protect task-time / PPS.
    droppedCount_++;
    return;
  }

  const Admit adm = admitClient(ip, nowMs);
  if (adm == Admit::Drop) {
    droppedCount_++;
    return;
  }
  if (adm == Admit::Deny) {
    deniedCount_++;
    sendKiss("DENY", vn, poll);
    return;
  }
  if (adm == Admit::Rate) {
    rateLimitedCount_++;
    sendKiss("RATE", vn, poll);
    return;
  }

  servedCount_++;
  sendNormal(gps, haveTime, recvSec, recvFrac);
}
