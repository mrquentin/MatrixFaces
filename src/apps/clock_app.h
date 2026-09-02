#pragma once

#include <cstdint>

#include "app.h"
#include "apps/settings_bag.h"
#include "board/time_source.h"

// Renders HH:MM:SS local time, centered on the panel, redrawing once per
// whole second so the app never contends with the HTTP server. TimeSource
// supplies the local breakdown; how the board works out its offset is not
// this app's concern.
class ClockApp : public App {
 public:
  explicit ClockApp(const TimeSource &clock);

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
  uint32_t lastRendered_ = kNeverRendered;
  int32_t colorRgb_ = 0x00b4ff;  // matches the previous hardcoded color565(0, 180, 255)
  int32_t textSize_ = 1;

  SettingsBag::Binding bindings_[2];
  SettingsBag settings_;
};
