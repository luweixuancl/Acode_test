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

  static volatile int8_t rotateAccum_;
  uint32_t lastBtnMs_ = 0;
  bool btnDown_ = false;
  bool clickPending_ = false;
  bool longPending_ = false;
  uint32_t pressStartMs_ = 0;
};
