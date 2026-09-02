#pragma once

#include <Client.h>

#include <cstdint>

// Local-time offset from UTC, resolved from the board's public IP via an
// outbound geolocation lookup (ip-api.com) and refreshed periodically so DST
// transitions self-correct without any hardcoded date math.
//
// This is purely a rendering-time adjustment for display apps. TimeSource
// itself always stays pure UTC -- HMAC request signing depends on that.
class TimezoneOffset {
 public:
  // `transport` is borrowed, not owned, and is reconnected on each lookup. It
  // must not be shared with another consumer: connecting tears down whatever
  // connection the instance was already holding.
  explicit TimezoneOffset(Client &transport) : transport_(transport) {}

  // Resolves on first call, then re-resolves periodically to track DST
  // changes. Safe to call every loop() iteration; it throttles itself.
  void maintain();

  bool isValid() const { return valid_; }

  // Seconds to add to a UTC epoch to get local time. Until the first
  // successful lookup this reports 0, i.e. callers render UTC meanwhile.
  int32_t offsetSeconds() const { return offsetSeconds_; }

 private:
  static constexpr uint32_t kResyncIntervalMs = 43200000;  // 12h: DST changes twice a year
  static constexpr uint32_t kRetryIntervalMs = 60000;      // until the first success

  bool resolve();

  Client &transport_;
  bool valid_ = false;
  int32_t offsetSeconds_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool attempted_ = false;
};
