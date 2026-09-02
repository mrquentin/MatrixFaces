#pragma once

#include <ctime>
#include <cstdint>

// Wall-clock time, always UTC. HMAC request signing depends on that: the
// replay window is compared against a real epoch, never a shifted one.
//
// Local time is a separate, display-only question, and how it is answered is
// board-specific -- a geolocation lookup on the M4, SNTP plus a POSIX TZ on
// the S3 -- so it lives behind localNow()/setTz() rather than in an app.
class TimeSource {
 public:
  // Tries to obtain the time. Safe to call repeatedly.
  bool sync();

  // Resyncs periodically, and refreshes whatever local-time information the
  // board needs; call from loop().
  void maintain();

  bool isValid() const { return valid_; }

  // Seconds since the Unix epoch, UTC. Only meaningful when isValid().
  uint32_t now() const;

  // Broken-down *local* time. False when the clock has not synced, in which
  // case `out` is untouched.
  //
  // Until the board has resolved its offset this reports UTC rather than
  // refusing, so a clock display comes up immediately and silently corrects
  // itself a moment later.
  bool localNow(std::tm &out) const;

  // Sets the local zone as a POSIX TZ string. Accepted and ignored on boards
  // that resolve their offset another way (see board_caps::kHasPosixTz).
  void setTz(const char *posixTz);

 private:
  static constexpr uint32_t kResyncIntervalMs = 3600000;  // one hour

  bool valid_ = false;
  uint32_t epochAtSync_ = 0;
  uint32_t millisAtSync_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool attempted_ = false;
};
