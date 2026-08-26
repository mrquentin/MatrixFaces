#pragma once

#include <cstdint>

#include "app.h"
#include "board/time_source.h"

// Renders HH:MM:SS, centered on the panel. TimeSource only reports UTC (no
// timezone database on the board), so that's what's shown; redraws happen
// once per whole second so the app never contends with the HTTP server.
class ClockApp : public App {
 public:
  explicit ClockApp(const TimeSource &clock) : clock_(clock) {}

  const char *name() const override { return "clock"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

 private:
  // Sentinels for lastRendered_, distinct from any real epoch second (those
  // values are ~130,000 years out) or the union of the two.
  static constexpr uint32_t kNeverRendered = 0xFFFFFFFFu;
  static constexpr uint32_t kNotSyncedRendered = 0xFFFFFFFEu;

  const TimeSource &clock_;
  uint32_t lastRendered_ = kNeverRendered;
};
