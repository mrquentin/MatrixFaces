#pragma once

#include <cstdint>

#include "app.h"
#include "board/time_source.h"
#include "apps/settings_bag.h"
#include "net/timezone_offset.h"

// Renders HH:MM:SS local time, centered on the panel, redrawing once per
// whole second so the app never contends with the HTTP server. TimeSource
// supplies UTC; `tz` supplies the UTC offset (0, i.e. UTC, until the first
// successful lookup resolves it).
class ClockApp : public App {
 public:
  ClockApp(const TimeSource &clock, const TimezoneOffset &tz);

  const char *name() const override { return "clock"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

  SettingsBag *settings() override { return &settings_; }
  void onSettingChanged(const char *key) override;

 private:
  // Sentinels for lastRendered_, distinct from any real epoch second (those
  // values are ~130,000 years out) or the union of the two.
  static constexpr uint32_t kNeverRendered = 0xFFFFFFFFu;
  static constexpr uint32_t kNotSyncedRendered = 0xFFFFFFFEu;

  const TimeSource &clock_;
  const TimezoneOffset &tz_;
  uint32_t lastRendered_ = kNeverRendered;
  int32_t colorRgb_ = 0x00b4ff;  // matches the previous hardcoded color565(0, 180, 255)
  int32_t textSize_ = 1;

  SettingsBag::Binding bindings_[2];
  SettingsBag settings_;
};
