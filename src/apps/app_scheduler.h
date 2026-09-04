#pragma once

#include <cstdint>

#include "matrix_gfx.h"

#include "app.h"

// Owns the matrix hardware and the fixed set of registered apps, and drives
// whichever one is active. Apps are registered once at startup; nothing here
// allocates, matching the rest of the firmware.
class AppScheduler {
 public:
  static constexpr uint8_t kMaxApps = 8;

  explicit AppScheduler(MatrixGfx &matrix) : matrix_(matrix) {}

  // Registers an app as a candidate for switchTo(). Returns false once
  // kMaxApps registrations have been made.
  bool add(App &app);

  // Brings up the matrix hardware and starts the first registered app.
  // Returns the underlying matrix begin() status so main() can halt on failure.
  MatrixBeginStatus begin();

  // Switches the active app by registration index, blanking the matrix
  // first. No-op if index is out of range or already active.
  void switchTo(uint8_t index);

  uint8_t activeIndex() const { return activeIndex_; }
  uint8_t count() const { return count_; }
  const char *activeName() const;

  // Name of the app at `index`, or "" if out of range.
  const char *name(uint8_t index) const;

  // Settings forwarding, bounds-checked against both the app index and (for
  // settingDescriptor) that app's own setting count. These are what let the
  // API layer discover and drive any app's configuration generically.
  //
  // validate and apply are separate so a request carrying several settings can
  // be checked in full before any of it lands. Phase 4 relies on the same split
  // to validate on the network task and write on the render task.
  uint8_t settingCount(uint8_t appIndex) const;
  const SettingDescriptor &settingDescriptor(uint8_t appIndex, uint8_t settingIndex) const;
  bool getSetting(uint8_t appIndex, const char *key, SettingValue &out) const;
  bool validateSetting(uint8_t appIndex, const char *key, const SettingValue &value) const;
  bool applySetting(uint8_t appIndex, const char *key, const SettingValue &value);

  // Call every loop() iteration.
  void update(uint32_t nowMs);

 private:
  const SettingsBag *bagFor(uint8_t appIndex) const;
  SettingsBag *bagFor(uint8_t appIndex);

  MatrixGfx &matrix_;
  App *apps_[kMaxApps] = {};
  uint8_t count_ = 0;
  uint8_t activeIndex_ = 0;
};
