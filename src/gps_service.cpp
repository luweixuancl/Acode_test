#include "gps_service.h"

volatile uint32_t GpsService::ppsMillis_ = 0;
volatile uint32_t GpsService::ppsCount_ = 0;
volatile bool GpsService::ppsFlag_ = false;

void IRAM_ATTR GpsService::onPpsIsr() {
  ppsMillis_ = millis();
  ppsCount_++;
  ppsFlag_ = true;
}

void GpsService::begin() {
  pinMode(PIN_GPS_PPS, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPpsIsr, RISING);
  gpsSerial_.begin(GPS_UART_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  Serial.printf("GPS UART%d RX=%d TX=%d baud=%d\n", GPS_UART_NUM, PIN_GPS_RX, PIN_GPS_TX,
                GPS_UART_BAUD);
}

void GpsService::loop() {
  parseNmea();

  if (ppsFlag_) {
    ppsFlag_ = false;
    status_.ppsSeen = true;
    status_.ppsCount = ppsCount_;
  }

  status_.satellites = gps_.satellites.isValid() ? gps_.satellites.value() : 0;
  status_.validFix = gps_.location.isValid() && gps_.date.isValid() && gps_.time.isValid();
  if (gps_.location.isValid()) {
    status_.lat = gps_.location.lat();
    status_.lon = gps_.location.lng();
  }
  if (gps_.hdop.isValid()) {
    status_.hdop = gps_.hdop.hdop();
  }

  if (gps_.date.isValid() && gps_.time.isValid()) {
    TinyGPSDate d = gps_.date;
    TinyGPSTime t = gps_.time;
    // Build UTC epoch roughly from TinyGPS fields
    // Use Time library style conversion via mktime-like approach
    struct tm tmUtc = {};
    tmUtc.tm_year = d.year() - 1900;
    tmUtc.tm_mon = d.month() - 1;
    tmUtc.tm_mday = d.day();
    tmUtc.tm_hour = t.hour();
    tmUtc.tm_min = t.minute();
    tmUtc.tm_sec = t.second();
    time_t epoch = mktime(&tmUtc);
    // mktime treats as local; ESP Arduino often uses UTC if TZ unset.
    // Force interpret as UTC by compensating with timezone offset if needed.
    // Safer: use timegm equivalent — compute manually.
    // Manual Julian-day style:
    int y = d.year();
    int m = d.month();
    int day = d.day();
    if (m <= 2) {
      y -= 1;
      m += 12;
    }
    int64_t a = y / 100;
    int64_t b = 2 - a + a / 4;
    int64_t jd = static_cast<int64_t>(365.25 * (y + 4716)) +
                 static_cast<int64_t>(30.6001 * (m + 1)) + day + b - 1524;
    int64_t daysSinceUnix = jd - 2440588;  // Unix epoch JD
    status_.utcEpoch = static_cast<uint32_t>(daysSinceUnix * 86400LL +
                                             t.hour() * 3600L + t.minute() * 60L + t.second());
    status_.ageMs = gps_.time.age();
    (void)epoch;
  } else {
    status_.ageMs = 0xFFFFFFFF;
  }

#if GPS_DEBUG
  const uint32_t now = millis();
  if (now - lastDebugMs_ >= 1000) {
    lastDebugMs_ = now;
    Serial.printf("GPS fix=%d sat=%u pps=%u age=%lu utc=%lu\n",
                  status_.validFix ? 1 : 0, status_.satellites, status_.ppsCount,
                  static_cast<unsigned long>(status_.ageMs),
                  static_cast<unsigned long>(status_.utcEpoch));
  }
#endif
}

void GpsService::parseNmea() {
  while (gpsSerial_.available() > 0) {
    const char c = static_cast<char>(gpsSerial_.read());
    gps_.encode(c);
#if GPS_DEBUG_NMEA
    Serial.write(c);
#endif
  }
}

bool GpsService::ppsFresh() const {
  if (ppsMillis_ == 0) {
    return false;
  }
  return (millis() - ppsMillis_) < 1500;
}

bool GpsService::nowUtc(uint32_t& seconds, uint32_t& fraction) const {
  if (!status_.validFix || status_.utcEpoch == 0) {
    return false;
  }

  // Align second boundary to last PPS when available.
  uint32_t baseSec = status_.utcEpoch;
  uint32_t ppsMs = ppsMillis_;
  uint32_t nowMs = millis();
  uint32_t sincePps = nowMs - ppsMs;

  if (status_.ppsSeen && sincePps < 1500) {
    // NMEA time is typically the second that just started at PPS.
    seconds = baseSec;
    // NTP 32-bit fraction of second
    fraction = static_cast<uint32_t>((static_cast<uint64_t>(sincePps) << 32) / 1000ULL);
    if (sincePps >= 1000) {
      seconds += sincePps / 1000;
      fraction = static_cast<uint32_t>((static_cast<uint64_t>(sincePps % 1000) << 32) / 1000ULL);
    }
  } else {
    seconds = baseSec;
    fraction = 0;
  }
  return true;
}
