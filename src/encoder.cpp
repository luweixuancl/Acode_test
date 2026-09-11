#include "encoder.h"
#include <esp_timer.h>

volatile int16_t EncoderInput::rotateAccum_ = 0;
volatile uint8_t EncoderInput::abState_ = 0;

// ISR-safe debounce (µs). Mechanical KY-040 bounce is typically <1 ms.
static constexpr uint32_t kEncDebounceUs = 1000;

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
  noInterrupts();
  int16_t v = rotateAccum_;
  rotateAccum_ = 0;
  interrupts();
  if (v > 0) {
    return 1;
  }
  if (v < 0) {
    return -1;
  }
  return 0;
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
