#include "gps_service.h"

volatile uint32_t GpsService::ppsMillis_ = 0;
volatile uint32_t GpsService::ppsCount_ = 0;
volatile bool GpsService::ppsFlag_ = false;
TaskHandle_t GpsService::timeTask_ = nullptr;

void IRAM_ATTR GpsService::onPpsIsr() {
  ppsMillis_ = millis();
  ppsCount_++;
  ppsFlag_ = true;
  BaseType_t woken = pdFALSE;
  if (timeTask_ != nullptr) {
    vTaskNotifyGiveFromISR(timeTask_, &woken);
  }
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

void GpsService::begin() {
  pinMode(PIN_GPS_PPS, INPUT_PULLDOWN);
  gpsSerial_.setRxBufferSize(2048);
  gpsSerial_.begin(GPS_UART_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPpsIsr, RISING);
  Serial.printf("GPS UART%d RX=%d TX=%d baud=%d buf=2048\n", GPS_UART_NUM, PIN_GPS_RX,
                PIN_GPS_TX, GPS_UART_BAUD);
}

void GpsService::loop() {
  parseNmea();

  if (ppsFlag_) {
    ppsFlag_ = false;
    ppsSeen_ = true;
  }

  GpsStatus work;
  work.satellites = gps_.satellites.isValid() ? gps_.satellites.value() : 0;
  work.validFix = gps_.location.isValid() && gps_.date.isValid() && gps_.time.isValid();
  if (gps_.location.isValid()) {
    work.lat = gps_.location.lat();
    work.lon = gps_.location.lng();
  }
  if (gps_.hdop.isValid()) {
    work.hdop = gps_.hdop.hdop();
  }
  work.ppsSeen = ppsSeen_;
  work.ppsCount = ppsCount_;
  work.ppsFresh = ppsFresh();

  if (gps_.date.isValid() && gps_.time.isValid()) {
    TinyGPSDate d = gps_.date;
    TinyGPSTime t = gps_.time;
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
    int64_t daysSinceUnix = jd - 2440588;
    const uint32_t epoch = static_cast<uint32_t>(daysSinceUnix * 86400LL + t.hour() * 3600L +
                                                 t.minute() * 60L + t.second());
    work.ageMs = gps_.time.age();
    if (epoch != lastCommittedSecond_) {
      commitNmeaTime(epoch);
      lastCommittedSecond_ = epoch;
    }
  } else {
    work.ageMs = 0xFFFFFFFF;
  }

  work.qualityMs = qualityMs();
  uint32_t sec = 0;
  uint32_t frac = 0;
  work.timeValid = nowUtc(sec, frac);
  work.utcEpoch = work.timeValid ? sec : commitEpoch_;

  publishStatus(work);

#if GPS_DEBUG
  const uint32_t now = millis();
  if (now - lastDebugMs_ >= 1000) {
    lastDebugMs_ = now;
    Serial.printf("GPS fix=%d sat=%u pps=%u age=%lu utc=%lu q=%lu valid=%d\n",
                  work.validFix ? 1 : 0, work.satellites, work.ppsCount,
                  static_cast<unsigned long>(work.ageMs),
                  static_cast<unsigned long>(work.utcEpoch),
                  static_cast<unsigned long>(work.qualityMs), work.timeValid ? 1 : 0);
  }
#endif
}

void GpsService::commitNmeaTime(uint32_t epochSec) {
  commitEpoch_ = epochSec;
  commitPpsCount_ = ppsCount_;
  commitMs_ = millis();
  haveCommit_ = true;
}

void GpsService::publishStatus(const GpsStatus& work) {
  portENTER_CRITICAL(&mux_);
  published_ = work;
  portEXIT_CRITICAL(&mux_);
}

void GpsService::parseNmea() {
  while (gpsSerial_.available() > 0) {
    gps_.encode(static_cast<char>(gpsSerial_.read()));
  }
}

GpsStatus GpsService::snapshot() const {
  GpsStatus out;
  portENTER_CRITICAL(&mux_);
  out = published_;
  portEXIT_CRITICAL(&mux_);
  return out;
}

bool GpsService::ppsFresh() const {
  if (ppsMillis_ == 0) {
    return false;
  }
  return (millis() - ppsMillis_) < 1500;
}

uint32_t GpsService::qualityMs() const {
  if (!haveCommit_) {
    return 0xFFFFFFFF;
  }
  const uint32_t age = millis() - commitMs_;
  if (age > 3000) {
    return 0xFFFFFFFF;
  }
  if (ppsFresh()) {
    const uint32_t sincePps = millis() - ppsMillis_;
    return sincePps < 1000 ? (sincePps + 5) : 50;
  }
  return 200 + age;
}

bool GpsService::nowUtc(uint32_t& seconds, uint32_t& fraction) const {
  if (!haveCommit_ || commitEpoch_ == 0) {
    return false;
  }
  if ((millis() - commitMs_) > 3000) {
    return false;
  }

  const uint32_t ppsNow = ppsCount_;
  const uint32_t lag = ppsNow - commitPpsCount_;
  if (lag > 60) {
    return false;
  }

  uint32_t baseSec = commitEpoch_ + lag;
  const uint32_t ppsMs = ppsMillis_;
  const uint32_t nowMs = millis();
  const uint32_t sincePps = nowMs - ppsMs;

  if (ppsSeen_ && sincePps < 1500) {
    seconds = baseSec;
    fraction = static_cast<uint32_t>((static_cast<uint64_t>(sincePps % 1000) << 32) / 1000ULL);
    if (sincePps >= 1000) {
      seconds += sincePps / 1000;
    }
  } else {
    seconds = commitEpoch_;
    fraction = 0;
  }
  return true;
}
