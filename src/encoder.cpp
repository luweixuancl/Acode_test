#include "encoder.h"

volatile int8_t EncoderInput::rotateAccum_ = 0;

void IRAM_ATTR EncoderInput::onEncIsr() {
  // Simple quadrature: sample B on A falling edge
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 2) {
    return;
  }
  lastMs = now;
  bool a = digitalRead(PIN_ENC_A);
  bool b = digitalRead(PIN_ENC_B);
  if (!a) {
    if (b) {
      rotateAccum_++;
    } else {
      rotateAccum_--;
    }
  }
}

void EncoderInput::begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onEncIsr, CHANGE);
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
  (void)lastBtnMs_;
}

int8_t EncoderInput::consumeRotate() {
  noInterrupts();
  int8_t v = rotateAccum_;
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
