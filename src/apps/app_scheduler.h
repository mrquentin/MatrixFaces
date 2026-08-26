#pragma once

#include <cstdint>

#include <Adafruit_Protomatter.h>

#include "app.h"

// Owns the matrix hardware and the fixed set of registered apps, and drives
// whichever one is active. Apps are registered once at startup; nothing here
// allocates, matching the rest of the firmware.
class AppScheduler {
 public:
  static constexpr uint8_t kMaxApps = 8;

  explicit AppScheduler(Adafruit_Protomatter &matrix) : matrix_(matrix) {}

  // Registers an app as a candidate for switchTo(). Returns false once
  // kMaxApps registrations have been made.
  bool add(App &app);

  // Brings up the matrix hardware and starts the first registered app.
  // Returns the underlying Protomatter status so main() can halt on failure.
  ProtomatterStatus begin();

  // Switches the active app by registration index, blanking the matrix
  // first. No-op if index is out of range or already active.
  void switchTo(uint8_t index);

  uint8_t activeIndex() const { return activeIndex_; }
  uint8_t count() const { return count_; }
  const char *activeName() const;

  // Name of the app at `index`, or "" if out of range.
  const char *name(uint8_t index) const;

  // Call every loop() iteration.
  void update(uint32_t nowMs);

 private:
  Adafruit_Protomatter &matrix_;
  App *apps_[kMaxApps] = {};
  uint8_t count_ = 0;
  uint8_t activeIndex_ = 0;
};
