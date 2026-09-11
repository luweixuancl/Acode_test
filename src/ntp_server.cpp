#include "ntp_server.h"
#include "config.h"
#include <WiFi.h>
#include <cstring>

void NtpServer::begin() {
  udp_.begin(NTP_UDP_PORT);
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

void NtpServer::loop(const GpsService& gps) {
  if (udp_.parsePacket()) {
    handlePacket(gps);
  }
}

void NtpServer::handlePacket(const GpsService& gps) {
  int len = udp_.read(packet_, sizeof(packet_));
  if (len < 48) {
    return;
  }
  requestCount_++;

  uint32_t recvSec = 0;
  uint32_t recvFrac = 0;
  const bool haveTime = gps.nowUtc(recvSec, recvFrac);
  const bool ppsOk = gps.ppsFresh();
  const uint32_t qMs = gps.qualityMs();

  uint32_t ntpSec = haveTime ? (recvSec + NTP_EPOCH_DELTA) : 0;

  // LI | VN | Mode
  uint8_t li = haveTime ? 0 : 3;  // 3 = unsynchronized
  uint8_t vn = (packet_[0] >> 3) & 0x07;
  if (vn < 1 || vn > 4) {
    vn = 3;
  }
  packet_[0] = static_cast<uint8_t>((li << 6) | (vn << 3) | 4);  // server mode
  packet_[1] = haveTime ? 1 : 16;                                // stratum
  packet_[2] = 4;                                                // poll
  // precision: PPS-anchored ~1ms (-10), NMEA-only ~15ms (-6)
  packet_[3] = haveTime ? (ppsOk ? static_cast<uint8_t>(-10) : static_cast<uint8_t>(-6)) : 0xEC;

  // Root delay = 0
  memset(packet_ + 4, 0, 4);

  // Root dispersion from qualityMs (NTP short format: 16.16 fixed, seconds).
  // Convert ms -> 16.16: (ms / 1000) << 16 ≈ ms * 65.536
  uint32_t dispersion = 0;
  if (!haveTime || qMs == 0xFFFFFFFF) {
    dispersion = 0xFFFF0000UL;  // large / unsync
  } else {
    uint64_t d = (static_cast<uint64_t>(qMs) * 65536ULL) / 1000ULL;
    if (d > 0xFFFFFFFFULL) {
      d = 0xFFFFFFFFULL;
    }
    dispersion = static_cast<uint32_t>(d);
  }
  writeU32(packet_, 8, dispersion);

  // Reference ID "GPSS"
  packet_[12] = 'G';
  packet_[13] = 'P';
  packet_[14] = 'S';
  packet_[15] = 'S';

  // Reference timestamp: PPS-aligned whole second when possible
  uint32_t refFrac = ppsOk ? 0 : recvFrac;
  writeTimestamp(packet_, 16, ntpSec, refFrac);

  // Originate = client's transmit
  memcpy(packet_ + 24, packet_ + 40, 8);

  // Receive / transmit timestamps
  writeTimestamp(packet_, 32, ntpSec, recvFrac);

  uint32_t txSec = 0;
  uint32_t txFrac = 0;
  if (!gps.nowUtc(txSec, txFrac)) {
    txSec = recvSec;
    txFrac = recvFrac;
  }
  writeTimestamp(packet_, 40, haveTime ? (txSec + NTP_EPOCH_DELTA) : 0, txFrac);

  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(packet_, 48);
  udp_.endPacket();
}
