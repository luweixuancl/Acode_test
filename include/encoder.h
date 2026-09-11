#pragma once

#include <Arduino.h>
#include "config.h"

class EncoderInput {
 public:
  void begin();
  void loop();

  // Consumes queued rotation: -1 / 0 / +1
  int8_t consumeRotate();
  bool consumeClick();
  bool consumeLongPress();

 private:
  static void IRAM_ATTR onEncIsr();
  void sampleButton();

  static volatile int16_t rotateAccum_;
  static volatile uint8_t abState_;  // bit1=A, bit0=B
  bool btnDown_ = false;
  bool clickPending_ = false;
  bool longPending_ = false;
  uint32_t pressStartMs_ = 0;
};
