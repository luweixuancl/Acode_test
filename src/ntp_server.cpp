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
  for (int i = 0; i < NTP_MAX_PACKETS_PER_LOOP; ++i) {
    if (!udp_.parsePacket()) {
      break;
    }
    handlePacket(gps);
  }
}

void NtpServer::handlePacket(const GpsService& gps) {
  // Stamp receive as early as possible (before reading the payload).
  uint32_t recvSec = 0;
  uint32_t recvFrac = 0;
  const bool haveTime = gps.nowUtc(recvSec, recvFrac);

  int len = udp_.read(packet_, sizeof(packet_));
  if (len < 48) {
    return;
  }
  requestCount_++;

  const bool ppsOk = gps.ppsFresh();
  const uint32_t qMs = gps.qualityMs();
  const ClockState clk = gps.snapshot().clockState;

  // LI | VN | Mode — only Locked/Degraded/Holdover are considered synchronized.
  // Holdover keeps LI=0 (RFC LI is leap-second warning, not "in holdover");
  // rising root dispersion + /status clock.state advertise free-run.
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
  packet_[0] = static_cast<uint8_t>((li << 6) | (vn << 3) | 4);  // server mode
  packet_[1] = syncOk ? 1 : 16;                                  // stratum
  // Echo client poll when plausible; otherwise advertise 16s (4).
  uint8_t poll = packet_[2];
  if (poll < 4 || poll > 17) {
    poll = 4;
  }
  packet_[2] = poll;
  // Unsync: coarse precision (not a fake sub-µs claim). Sync: PPS → -10 else -6.
  packet_[3] = syncOk ? (ppsOk ? static_cast<uint8_t>(-10) : static_cast<uint8_t>(-6))
                       : static_cast<uint8_t>(-6);

  memset(packet_ + 4, 0, 4);  // root delay

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

  // Reference ID: GPSS when sync; INIT when unsynchronized (honest kiss).
  if (syncOk) {
    packet_[12] = 'G';
    packet_[13] = 'P';
    packet_[14] = 'S';
    packet_[15] = 'S';
  } else {
    packet_[12] = 'I';
    packet_[13] = 'N';
    packet_[14] = 'I';
    packet_[15] = 'T';
  }

  // Reference timestamp = last PPS-aligned UTC second (frac 0).
  uint32_t refSec = 0;
  uint32_t refFrac = 0;
  if (syncOk && gps.referenceUtc(refSec, refFrac)) {
    writeTimestamp(packet_, 16, refSec + NTP_EPOCH_DELTA, refFrac);
  } else {
    writeTimestamp(packet_, 16, ntpSec, syncOk ? ntpFrac : 0);
  }

  // Originate = client's transmit
  memcpy(packet_ + 24, packet_ + 40, 8);

  // Receive timestamp
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
