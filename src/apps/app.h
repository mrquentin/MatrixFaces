#pragma once

#include <cstdint>

#include <Adafruit_Protomatter.h>

#include "app_setting.h"

// Common interface every swappable matrix app implements. AppScheduler owns
// exactly one active app at a time and drives it every loop() iteration.
class App {
 public:
  virtual ~App() = default;

  // Short identifier used in logs and the /api/app switch endpoint.
  virtual const char *name() const = 0;

  // Called once when this app becomes active. The matrix is already blanked
  // by the scheduler; use this to reset any per-app render state so the next
  // update() redraws immediately instead of waiting for its own throttling.
  virtual void begin(Adafruit_Protomatter &matrix) { (void)matrix; }

  // Called every loop() iteration while this app is active. Implementations
  // should throttle their own redraws and only call matrix.show() when the
  // frame actually changed, since loop() also serves HTTP requests.
  virtual void update(Adafruit_Protomatter &matrix, uint32_t nowMs) = 0;

  // Optional configuration, discoverable over /api/apps without the caller
  // needing per-app knowledge: an app with settings describes each one's
  // key/type/constraints, and the API layer drives getSetting()/setSetting()
  // purely by key. Apps with none simply don't override any of the four
  // methods below; the defaults report zero settings.
  virtual uint8_t settingCount() const { return 0; }

  // Only valid for index < settingCount().
  virtual const SettingDescriptor &settingDescriptor(uint8_t index) const {
    (void)index;
    static constexpr SettingDescriptor kNone{"", "", SettingType::kBool, 0, 0, 0};
    return kNone;
  }

  // Reads the current value of `key`. Returns false if `key` is unknown.
  virtual bool getSetting(const char *key, SettingValue &out) const {
    (void)key;
    (void)out;
    return false;
  }

  // Validates and applies `value` for `key`. Returns false -- and leaves the
  // setting unchanged -- if `key` is unknown or `value` fails the
  // descriptor's constraints (wrong type, out of range, too long).
  virtual bool setSetting(const char *key, const SettingValue &value) {
    (void)key;
    (void)value;
    return false;
  }
};
