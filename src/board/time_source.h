#pragma once

#include <cstdint>

// Wall-clock time for the replay window, taken from the NINA co-processor's NTP
// sync and extrapolated with millis() between resyncs.
class TimeSource {
 public:
  // Tries to obtain the time from the WiFi module. Safe to call repeatedly.
  bool sync();

  // Resyncs periodically; call from loop().
  void maintain();

  bool isValid() const { return valid_; }

  // Seconds since the Unix epoch. Only meaningful when isValid().
  uint32_t now() const;

 private:
  static constexpr uint32_t kResyncIntervalMs = 3600000;  // one hour

  bool valid_ = false;
  uint32_t epochAtSync_ = 0;
  uint32_t millisAtSync_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool attempted_ = false;
};
