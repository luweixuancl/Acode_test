#include "gps_service.h"
#include <esp_timer.h>

portMUX_TYPE GpsService::ppsMux_ = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t GpsService::ppsMillis_ = 0;
volatile uint32_t GpsService::ppsCount_ = 0;
volatile uint64_t GpsService::ppsEdgeUs_ = 0;
volatile bool GpsService::ppsFlag_ = false;
TaskHandle_t GpsService::timeTask_ = nullptr;

void IRAM_ATTR GpsService::onPpsIsr() {
  const uint64_t edgeUs = esp_timer_get_time();
  const uint32_t nowMs = millis();
  portENTER_CRITICAL_ISR(&ppsMux_);
  ppsEdgeUs_ = edgeUs;
  ppsMillis_ = nowMs;
  ppsCount_++;
  ppsFlag_ = true;
  portEXIT_CRITICAL_ISR(&ppsMux_);
  BaseType_t woken = pdFALSE;
  if (timeTask_ != nullptr) {
    vTaskNotifyGiveFromISR(timeTask_, &woken);
  }
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

void GpsService::begin() {
  localClock_.reset();
  pinMode(PIN_GPS_PPS, INPUT_PULLDOWN);
  gpsSerial_.setRxBufferSize(2048);
  gpsSerial_.begin(GPS_UART_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPpsIsr, RISING);
  Serial.printf("GPS UART%d RX=%d TX=%d baud=%d buf=2048 local-clock=on\n", GPS_UART_NUM,
                PIN_GPS_RX, PIN_GPS_TX, GPS_UART_BAUD);
}

void GpsService::loop(AnomalyPolicy policy, uint16_t holdoverSec) {
  parseNmea();

  bool gotPps = false;
  uint64_t edgeUs = 0;
  uint32_t count = 0;
  portENTER_CRITICAL(&ppsMux_);
  if (ppsFlag_) {
    ppsFlag_ = false;
    edgeUs = ppsEdgeUs_;
    count = ppsCount_;
    gotPps = true;
  }
  portEXIT_CRITICAL(&ppsMux_);
  if (gotPps) {
    ppsSeen_ = true;
    localClock_.onPpsEdge(edgeUs, count);
  }

  GpsStatus work;
  work.satellites = gps_.satellites.isValid() ? gps_.satellites.value() : 0;
  // isValid() alone is sticky after first fix; require recent updates and sats>0 so
  // antenna-loss (0 sats / stale NMEA) clears "GPS 锁定" on OLED/Web.
  const bool locFresh =
      gps_.location.isValid() && gps_.location.age() <= GPS_FIX_MAX_AGE_MS;
  const bool timeFresh = gps_.date.isValid() && gps_.time.isValid() &&
                         gps_.time.age() <= GPS_FIX_MAX_AGE_MS;
  const bool satsOk = gps_.satellites.isValid() &&
                      gps_.satellites.age() <= GPS_FIX_MAX_AGE_MS && work.satellites > 0;
  work.validFix = locFresh && timeFresh && satsOk;
  if (locFresh) {
    work.lat = gps_.location.lat();
    work.lon = gps_.location.lng();
  }
  if (gps_.hdop.isValid() && gps_.hdop.age() <= GPS_FIX_MAX_AGE_MS) {
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
      commitNmeaTime(epoch, policy, holdoverSec);
      lastCommittedSecond_ = epoch;
    }
  } else {
    work.ageMs = 0xFFFFFFFF;
  }

  const bool nmeaFresh = haveCommit_ && (millis() - commitMs_) <= 3000;
  localClock_.tick(nmeaFresh, work.ppsFresh, policy, holdoverSec);

  work.qualityMs = qualityMs();
  uint32_t sec = 0;
  uint32_t frac = 0;
  work.timeValid = nowUtc(sec, frac);
  work.utcEpoch = work.timeValid ? sec : commitEpoch_;
  work.clockState = localClock_.state();
  work.residualMs = localClock_.residualMs();
  work.freqPpm = localClock_.freqPpm();
  work.holdoverMs = localClock_.holdoverElapsedMs();

  publishStatus(work);

#if GPS_DEBUG
  const uint32_t now = millis();
  if (now - lastDebugMs_ >= 1000) {
    lastDebugMs_ = now;
    Serial.printf(
        "GPS fix=%d sat=%u pps=%u utc=%lu valid=%d clk=%s r=%ld ppm=%.1f q=%lu hold=%lu\n",
        work.validFix ? 1 : 0, work.satellites, work.ppsCount,
        static_cast<unsigned long>(work.utcEpoch), work.timeValid ? 1 : 0,
        clockStateLabel(work.clockState), static_cast<long>(work.residualMs),
        static_cast<double>(work.freqPpm), static_cast<unsigned long>(work.qualityMs),
        static_cast<unsigned long>(work.holdoverMs));
  }
#endif
}

void GpsService::commitNmeaTime(uint32_t epochSec, AnomalyPolicy policy, uint16_t holdoverSec) {
  commitEpoch_ = epochSec;
  commitPpsCount_ = ppsCount_;
  commitMs_ = millis();
  haveCommit_ = true;
  localClock_.onNmeaCommit(epochSec, commitPpsCount_, policy, holdoverSec);
}

void GpsService::publishStatus(const GpsStatus& work) {
  portENTER_CRITICAL(&mux_);
  published_ = work;
  portEXIT_CRITICAL(&mux_);
}

void GpsService::parseNmea() {
  int budget = GPS_NMEA_MAX_BYTES_PER_LOOP;
  while (budget-- > 0 && gpsSerial_.available() > 0) {
    const char c = static_cast<char>(gpsSerial_.read());
#if GPS_DEBUG_NMEA
    Serial.write(c);
#endif
    gps_.encode(c);
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
  return localClock_.qualityMs();
}

bool GpsService::nowUtc(uint32_t& seconds, uint32_t& fraction) const {
  // NTP honesty: only LocalClock Locked/Degraded/Holdover may serve time.
  return localClock_.nowUtc(seconds, fraction);
}
