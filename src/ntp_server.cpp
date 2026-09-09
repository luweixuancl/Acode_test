#include "ntp_server.h"
#include "config.h"
#include <WiFi.h>
#include <cstring>

void NtpServer::begin() {
  udp_.begin(NTP_UDP_PORT);
}

void NtpServer::writeTimestamp(uint8_t* pkt, int offset, uint32_t sec, uint32_t frac) {
  pkt[offset + 0] = (sec >> 24) & 0xFF;
  pkt[offset + 1] = (sec >> 16) & 0xFF;
  pkt[offset + 2] = (sec >> 8) & 0xFF;
  pkt[offset + 3] = sec & 0xFF;
  pkt[offset + 4] = (frac >> 24) & 0xFF;
  pkt[offset + 5] = (frac >> 16) & 0xFF;
  pkt[offset + 6] = (frac >> 8) & 0xFF;
  pkt[offset + 7] = frac & 0xFF;
}

void NtpServer::loop(const GpsService& gps) {
  if (!WiFi.isConnected() && WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
    // Still answer in STA or AP modes; STA required for LAN clients usually.
  }
  if (udp_.parsePacket()) {
    handlePacket(gps);
  }
}

void NtpServer::handlePacket(const GpsService& gps) {
  int len = udp_.read(packet_, sizeof(packet_));
  if (len < 48) {
    return;
  }

  uint32_t recvSec = 0;
  uint32_t recvFrac = 0;
  bool haveTime = gps.nowUtc(recvSec, recvFrac);
  uint32_t ntpSec = haveTime ? (recvSec + NTP_EPOCH_DELTA) : 0;

  // LI | VN | Mode
  uint8_t li = haveTime ? 0 : 3;  // 3 = unsynchronized
  uint8_t vn = (packet_[0] >> 3) & 0x07;
  if (vn < 1 || vn > 4) {
    vn = 3;
  }
  packet_[0] = static_cast<uint8_t>((li << 6) | (vn << 3) | 4);  // server mode
  packet_[1] = haveTime ? 1 : 16;  // stratum
  packet_[2] = 4;                  // poll
  packet_[3] = haveTime ? -6 : 0xEC;  // precision ~15ms without fine clock, -6 => ~15ms
  // Root delay / dispersion
  memset(packet_ + 4, 0, 8);
  // Reference ID "GPSS"
  packet_[12] = 'G';
  packet_[13] = 'P';
  packet_[14] = 'S';
  packet_[15] = 'S';

  // Reference timestamp
  writeTimestamp(packet_, 16, ntpSec, recvFrac);
  // Originate = client's transmit (bytes 40-47 of request already in place as originate after copy)
  // Request layout: transmit at 40. Move to originate at 24.
  memcpy(packet_ + 24, packet_ + 40, 8);
  // Receive timestamp
  writeTimestamp(packet_, 32, ntpSec, recvFrac);
  // Transmit timestamp (approx same)
  uint32_t txSec = 0;
  uint32_t txFrac = 0;
  if (!gps.nowUtc(txSec, txFrac)) {
    txSec = recvSec;
    txFrac = recvFrac;
  }
  writeTimestamp(packet_, 40, txSec + NTP_EPOCH_DELTA, txFrac);

  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(packet_, 48);
  udp_.endPacket();
}
