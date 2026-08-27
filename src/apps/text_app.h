#pragma once

#include <cstdint>

#include "app.h"

// Displays a fixed, configurable message centered on the panel. Exists as a
// concrete demonstration of App's settings mechanism: "text" (string) and
// "size" (int, GFX text scale) are both discoverable over /api/apps and
// settable over /api/apps/<index>/settings without any app-specific code in
// main.cpp.
class TextApp : public App {
 public:
  static constexpr size_t kTextCap = 32;

  const char *name() const override { return "text"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

  uint8_t settingCount() const override { return 2; }
  const SettingDescriptor &settingDescriptor(uint8_t index) const override;
  bool getSetting(const char *key, SettingValue &out) const override;
  bool setSetting(const char *key, const SettingValue &value) override;

 private:
  char text_[kTextCap] = "Hello!";
  int32_t size_ = 1;
  bool dirty_ = true;
};
