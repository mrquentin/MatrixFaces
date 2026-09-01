#include "time_source.h"

#include <Arduino.h>
#include <WiFiNINA.h>

namespace {

// Anything below this is the module reporting "no time yet" rather than a real
// timestamp, so treat it as a failed sync.
constexpr uint32_t kPlausibleEpochFloor = 1600000000U;  // 2020-09-13

}  // namespace

bool TimeSource::sync() {
  lastAttemptMs_ = millis();
  attempted_ = true;

  const uint32_t epoch = WiFi.getTime();
  if (epoch < kPlausibleEpochFloor) return false;

  epochAtSync_ = epoch;
  millisAtSync_ = millis();
  valid_ = true;
  return true;
}

void TimeSource::maintain() {
  const uint32_t interval = valid_ ? kResyncIntervalMs : 30000;
  if (attempted_ && millis() - lastAttemptMs_ < interval) return;
  sync();
}

uint32_t TimeSource::now() const {
  if (!valid_) return 0;
  // Unsigned subtraction stays correct across the millis() rollover.
  return epochAtSync_ + (millis() - millisAtSync_) / 1000;
}
