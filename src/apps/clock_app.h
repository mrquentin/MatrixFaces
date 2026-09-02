#pragma once

#include <cstdint>

#include "app.h"
#include "apps/settings_bag.h"
#include "board_caps.h"
#include "board/time_source.h"

// Renders HH:MM:SS local time, centered on the panel, redrawing once per
// whole second so the app never contends with the HTTP server. TimeSource
// supplies the local breakdown; how the board works out its offset is not
// this app's concern.
//
// Except in one respect. On a board that takes a POSIX TZ string the zone is
// a choice somebody has to make, and a clock is where you would look for it,
// so this app carries it as a setting and hands it to TimeSource. Boards that
// resolve their offset another way do not advertise the setting at all --
// better than offering a control that is quietly ignored.
class ClockApp : public App {
 public:
  explicit ClockApp(TimeSource &clock);

  const char *name() const override { return "clock"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

  SettingsBag *settings() override { return &settings_; }
  void onSettingChanged(const char *key) override;

 private:
  // Sentinels for lastRendered_, distinct from any seconds-of-day value.
  static constexpr uint32_t kNeverRendered = 0xFFFFFFFFu;
  static constexpr uint32_t kNotSyncedRendered = 0xFFFFFFFEu;

  // How often to bother asking what time it is. A clock changes once a second,
  // so twenty looks per second is already twenty times more than it can use,
  // and the second boundary lands within 50 ms of where it should -- which is
  // not a thing anyone can see.
  //
  // Without this the breakdown ran on every loop() iteration, about 11,000
  // times a second on the S3, to discover nothing had changed. That is not
  // free: newlib's localtime_r re-reads TZ and applies the zone's DST rules on
  // every call, and spending that beside a refresh ISR whose scanline timing
  // is *estimated* rather than measured is worse than merely wasteful.
  static constexpr uint32_t kPollIntervalMs = 50;

  // The zone binding is built either way -- it is one array element -- but it
  // is only counted into the bag, and so only visible over the API, where the
  // board can act on it.
  static constexpr uint8_t kSettingCount = board_caps::kHasPosixTz ? 3 : 2;

  TimeSource &clock_;
  uint32_t lastRendered_ = kNeverRendered;
  uint32_t lastPolledMs_ = 0;
  int32_t colorRgb_ = 0x00b4ff;  // matches the previous hardcoded color565(0, 180, 255)
  int32_t textSize_ = 1;
  // A POSIX TZ string, empty meaning UTC. Comfortably inside the 32-byte cap a
  // SettingValue can carry: "CET-1CEST,M3.5.0,M10.5.0/3" is 26.
  char tz_[SettingValue::kStringCap] = "";

  SettingsBag::Binding bindings_[3];
  SettingsBag settings_;
};
