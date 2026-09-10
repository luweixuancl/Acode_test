#include "status_leds.h"
#include "config.h"

void StatusLeds::begin() {
  pinMode(PIN_LED_D4, OUTPUT);
  pinMode(PIN_LED_D5, OUTPUT);
  digitalWrite(PIN_LED_D4, LOW);
  digitalWrite(PIN_LED_D5, LOW);
}

void StatusLeds::writeBlink(uint8_t pin, uint32_t nowMs, uint32_t halfPeriodMs) {
  const bool on = ((nowMs / halfPeriodMs) % 2) == 0;
  digitalWrite(pin, on ? HIGH : LOW);
}

void StatusLeds::loop(bool apMode, bool wifiStaOk, const GpsService& gps) {
  const uint32_t now = millis();
  if (now - lastUpdateMs_ < 20) {
    return;
  }
  lastUpdateMs_ = now;

  // D4 RUN: fast blink in AP/setup, slow blink if no STA, solid when WiFi is up.
  if (wifiStaOk) {
    digitalWrite(PIN_LED_D4, HIGH);
  } else if (apMode) {
    writeBlink(PIN_LED_D4, now, 120);
  } else {
    writeBlink(PIN_LED_D4, now, 500);
  }

  // D5 SYNC: off = no fix, blink = fix without PPS, solid = Stratum-1 ready.
  const GpsStatus& st = gps.status();
  if (st.validFix && gps.ppsFresh()) {
    digitalWrite(PIN_LED_D5, HIGH);
  } else if (st.validFix || st.satellites > 0) {
    writeBlink(PIN_LED_D5, now, 400);
  } else {
    digitalWrite(PIN_LED_D5, LOW);
  }
}
