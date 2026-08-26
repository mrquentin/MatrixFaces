#pragma once

#include <cstdint>

// Debounced active-low button. The Matrix Portal M4's UP and DOWN buttons pull
// their pin to ground, so they run with the internal pull-up enabled.
class Button {
 public:
  void begin(uint8_t pin);
  void poll();

  // True once per press, consumed by the call.
  bool takePress();

  // True once each time the button stays held for `ms`, consumed by the call.
  bool takeHold(uint32_t ms);

 private:
  static constexpr uint32_t kDebounceMs = 30;

  uint8_t pin_ = 0;
  bool rawLevel_ = true;     // pin reading, HIGH when released
  bool stableDown_ = false;  // debounced state
  uint32_t lastChangeMs_ = 0;
  uint32_t pressedAtMs_ = 0;
  bool pressPending_ = false;
  bool holdConsumed_ = false;
};
