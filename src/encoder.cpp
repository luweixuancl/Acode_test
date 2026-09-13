#include "encoder.h"
#include <esp_timer.h>

volatile int16_t EncoderInput::rotateAccum_ = 0;
volatile uint8_t EncoderInput::abState_ = 0;

// ISR-safe edge floor (µs). Finer filtering is in drainRawToFilter / consumeRotate.
static constexpr uint32_t kEncDebounceUs = ENC_ISR_DEBOUNCE_US;

// Quadrature transition table indexed by (prev<<2)|curr; values -1/0/+1.
// prev/curr are 2-bit Gray codes: bit1=A, bit0=B.
static const int8_t kQuadTable[16] = {
    0, -1, +1, 0,  // 00 -> 00,01,10,11
    +1, 0, 0, -1,  // 01 -> 00,01,10,11
    -1, 0, 0, +1,  // 10 -> 00,01,10,11
    0, +1, -1, 0,  // 11 -> 00,01,10,11
};

void IRAM_ATTR EncoderInput::onEncIsr() {
  static uint64_t lastUs = 0;
  const uint64_t nowUs = esp_timer_get_time();
  if (nowUs - lastUs < kEncDebounceUs) {
    return;
  }
  lastUs = nowUs;

  const uint8_t a = digitalRead(PIN_ENC_A) ? 1 : 0;
  const uint8_t b = digitalRead(PIN_ENC_B) ? 1 : 0;
  const uint8_t curr = static_cast<uint8_t>((a << 1) | b);
  const uint8_t prev = abState_;
  if (curr == prev) {
    return;
  }

  const int8_t step = kQuadTable[(prev << 2) | curr];
  abState_ = curr;
  if (step == 0) {
    return;
  }

  const int16_t next = static_cast<int16_t>(rotateAccum_ + step);
  if (next > 32000 || next < -32000) {
    return;
  }
  rotateAccum_ = next;
}

void EncoderInput::begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  const uint8_t a = digitalRead(PIN_ENC_A) ? 1 : 0;
  const uint8_t b = digitalRead(PIN_ENC_B) ? 1 : 0;
  abState_ = static_cast<uint8_t>((a << 1) | b);

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onEncIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), onEncIsr, CHANGE);
}

void EncoderInput::loop() {
  sampleButton();
  drainRawToFilter();
}

void EncoderInput::drainRawToFilter() {
  noInterrupts();
  const int16_t raw = rotateAccum_;
  rotateAccum_ = 0;
  interrupts();

  const uint32_t now = millis();
  if (raw != 0) {
    int32_t next = static_cast<int32_t>(filtAccum_) + raw;
    if (next > 32000) {
      next = 32000;
    } else if (next < -32000) {
      next = -32000;
    }
    filtAccum_ = static_cast<int16_t>(next);
    lastMotionMs_ = now;
    return;
  }
  if (filtAccum_ != 0 &&
      static_cast<int32_t>(now - lastMotionMs_) >= static_cast<int32_t>(ENC_IDLE_CLEAR_MS) &&
      abs(filtAccum_) < ENC_DETENT_STEPS) {
    filtAccum_ = 0;
  }
}

void EncoderInput::sampleButton() {
  bool down = digitalRead(PIN_ENC_SW) == LOW;
  uint32_t now = millis();
  if (down && !btnDown_) {
    btnDown_ = true;
    pressStartMs_ = now;
  } else if (!down && btnDown_) {
    btnDown_ = false;
    uint32_t held = now - pressStartMs_;
    if (held >= 800) {
      longPending_ = true;
    } else if (held >= 30) {
      clickPending_ = true;
    }
  } else if (down && btnDown_ && !longPending_ && (now - pressStartMs_ >= 800)) {
    longPending_ = true;
  }
}

int8_t EncoderInput::consumeRotate() {
  const int16_t th = static_cast<int16_t>(ENC_DETENT_STEPS);
  if (filtAccum_ < th && filtAccum_ > -th) {
    return 0;
  }

  const uint32_t now = millis();
  const bool fastSpin = abs(filtAccum_) >= (th * 2);
  if (!fastSpin && lastEmitMs_ != 0 &&
      static_cast<int32_t>(now - lastEmitMs_) < static_cast<int32_t>(ENC_MIN_STEP_MS)) {
    return 0;
  }

  if (filtAccum_ >= th) {
    filtAccum_ = static_cast<int16_t>(filtAccum_ - th);
    if (!fastSpin && abs(filtAccum_) < th) {
      filtAccum_ = 0;
    }
    lastEmitMs_ = now;
    return 1;
  }

  filtAccum_ = static_cast<int16_t>(filtAccum_ + th);
  if (!fastSpin && abs(filtAccum_) < th) {
    filtAccum_ = 0;
  }
  lastEmitMs_ = now;
  return -1;
}

bool EncoderInput::consumeClick() {
  if (clickPending_) {
    clickPending_ = false;
    return true;
  }
  return false;
}

bool EncoderInput::consumeLongPress() {
  if (longPending_) {
    longPending_ = false;
    clickPending_ = false;
    return true;
  }
  return false;
}
