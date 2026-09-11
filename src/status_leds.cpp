#include "status_leds.h"
#include "app_ipc.h"
#include "config.h"

void StatusLeds::begin() {
  pinMode(PIN_LED_D4, OUTPUT);
  pinMode(PIN_LED_D5, OUTPUT);
  digitalWrite(PIN_LED_D4, LOW);
  digitalWrite(PIN_LED_D5, LOW);
  const uint32_t now = millis();
  gIpc.kickTimeMs = now;
  gIpc.kickNetMs = now;
  gIpc.kickUiMs = now;
}

void StatusLeds::writeBlink(uint8_t pin, uint32_t nowMs, uint32_t halfPeriodMs) {
  const bool on = ((nowMs / halfPeriodMs) % 2) == 0;
  digitalWrite(pin, on ? HIGH : LOW);
}

void StatusLeds::writeHeartbeat(uint8_t pin, uint32_t nowMs) {
  const uint32_t period = LED_HEARTBEAT_ON_MS + LED_HEARTBEAT_OFF_MS;
  const bool on = (nowMs % period) < LED_HEARTBEAT_ON_MS;
  digitalWrite(pin, on ? HIGH : LOW);
}

bool StatusLeds::tasksStale(uint32_t nowMs) {
  const uint32_t t = gIpc.kickTimeMs;
  const uint32_t n = gIpc.kickNetMs;
  const uint32_t u = gIpc.kickUiMs;
  // Ignore until all tasks have kicked at least once (0 = not started).
  if (t == 0 || n == 0 || u == 0) {
    return false;
  }
  auto aged = [nowMs](uint32_t kick) -> bool {
    return (nowMs - kick) > LED_TASK_STALE_MS;
  };
  return aged(t) || aged(n) || aged(u);
}

void StatusLeds::loop(bool apMode, bool wifiStaOk, const GpsStatus& st) {
  const uint32_t now = millis();
  if (now - lastUpdateMs_ < 20) {
    return;
  }
  lastUpdateMs_ = now;

  // Panic: another task stopped kicking — alternate D4/D5 ~5 Hz.
  if (tasksStale(now)) {
    const bool phase = ((now / LED_PANIC_HALF_PERIOD_MS) % 2) == 0;
    digitalWrite(PIN_LED_D4, phase ? HIGH : LOW);
    digitalWrite(PIN_LED_D5, phase ? LOW : HIGH);
    return;
  }

  if (wifiStaOk) {
    writeHeartbeat(PIN_LED_D4, now);
  } else if (apMode) {
    writeBlink(PIN_LED_D4, now, 120);
  } else {
    writeBlink(PIN_LED_D4, now, 500);
  }

  if (st.validFix && st.ppsFresh && st.timeValid &&
      (st.clockState == ClockState::Locked || st.clockState == ClockState::Degraded)) {
    // Opposite phase to D4 so dual-heartbeat is easier to see as "alive".
    writeHeartbeat(PIN_LED_D5, now + LED_HEARTBEAT_ON_MS / 2);
  } else if (st.clockState == ClockState::Holdover) {
    writeBlink(PIN_LED_D5, now, 200);  // fast blink = holdover
  } else if (st.validFix || st.satellites > 0 || st.clockState == ClockState::Acquiring) {
    writeBlink(PIN_LED_D5, now, 400);
  } else {
    digitalWrite(PIN_LED_D5, LOW);
  }
}
