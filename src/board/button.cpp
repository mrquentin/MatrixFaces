#include "button.h"

#include <Arduino.h>

void Button::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
  rawLevel_ = digitalRead(pin_) != LOW;
  stableDown_ = !rawLevel_;
  lastChangeMs_ = millis();
}

void Button::poll() {
  const bool level = digitalRead(pin_) != LOW;
  const uint32_t nowMs = millis();

  if (level != rawLevel_) {
    rawLevel_ = level;
    lastChangeMs_ = nowMs;
    return;
  }

  if (nowMs - lastChangeMs_ < kDebounceMs) return;

  const bool down = !level;
  if (down == stableDown_) return;

  stableDown_ = down;
  if (down) {
    pressedAtMs_ = nowMs;
    pressPending_ = true;
    holdConsumed_ = false;
  }
}

bool Button::takePress() {
  if (!pressPending_) return false;
  pressPending_ = false;
  return true;
}

bool Button::takeHold(uint32_t ms) {
  if (!stableDown_ || holdConsumed_) return false;
  if (millis() - pressedAtMs_ < ms) return false;

  holdConsumed_ = true;
  return true;
}
