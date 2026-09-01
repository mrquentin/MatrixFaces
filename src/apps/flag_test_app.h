#pragma once

#include <cstdint>

#include "app.h"
#include "flag_display.h"

// Cycles through every flag rendered by drawFlag() (see flag_display.h),
// holding each for a few seconds, purely so the flag designs can be tuned
// by eye on the actual panel without needing a live MultiViewer session.
// No settings, no network -- swap it in/out of AppScheduler as needed.
class FlagTestApp : public App {
 public:
  const char *name() const override { return "flagtest"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

 private:
  static constexpr uint32_t kHoldMs = 3000;
  static constexpr FlagKind kSequence[] = {
      FlagKind::kYellow, FlagKind::kSafetyCar, FlagKind::kVirtualSafetyCar, FlagKind::kRed, FlagKind::kBlue,
  };
  static constexpr uint8_t kSequenceLen = 5;

  void drawCurrent(Adafruit_Protomatter &matrix) const;

  uint8_t index_ = 0;
  uint32_t lastSwitchMs_ = 0;
  bool everRendered_ = false;
};
