#pragma once

#include <cstdint>

#include "app.h"
#include "apps/settings_bag.h"

// Displays a configurable message: centered and static if it fits the panel,
// otherwise scrolling horizontally in a continuous loop. Demonstrates App's
// settings mechanism across all three non-bool types: "text" (string),
// "size" (int, GFX text scale), "color" (color).
class TextApp : public App {
 public:
  static constexpr size_t kTextCap = 32;

  TextApp();

  const char *name() const override { return "text"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

  SettingsBag *settings() override { return &settings_; }
  void onSettingChanged(const char *key) override;

 private:
  static constexpr uint32_t kScrollIntervalMs = 30;  // 1px/frame, ~33px/s

  // Recomputes bounds and scroll-vs-static mode from the current text/size.
  void layout(Adafruit_Protomatter &matrix);
  void draw(Adafruit_Protomatter &matrix, int16_t x, int16_t y);

  char text_[kTextCap] = "Hello!";
  int32_t size_ = 1;
  int32_t colorRgb_ = 0xff8c00;  // matches the previous hardcoded color565(255, 140, 0)

  bool needsLayout_ = true;
  bool scrolling_ = false;
  bool staticDrawn_ = false;  // only meaningful when !scrolling_
  int16_t staticX_ = 0;
  int16_t textY_ = 0;
  int16_t scrollX_ = 0;
  int16_t scrollMinX_ = 0;  // scrollX_ below this means fully off the left edge
  uint32_t lastFrameMs_ = 0;

  SettingsBag::Binding bindings_[3];
  SettingsBag settings_;
};
