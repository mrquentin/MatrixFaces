#include "board/time_source.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

// UTC from SNTP, local time from a POSIX TZ string. Between them these two
// replace the M4's whole arrangement -- a one-shot epoch read from the WiFi
// co-processor plus a geolocation lookup for the offset -- and take the
// ip-api.com request, and the public IP it leaked, with them.
//
// The zone string comes from the clock app's "tz" setting, which is only
// offered on boards where board_caps::kHasPosixTz says it means something.
namespace {

// Anything below this is the system clock reporting "never set" rather than a
// real timestamp, so treat it as a failed sync.
constexpr uint32_t kPlausibleEpochFloor = 1600000000U;  // 2020-09-13

// How often to look again while the clock is still unset. The SNTP client is
// doing the actual retrying; this only asks whether it has landed yet.
constexpr uint32_t kRecheckIntervalMs = 1000;

constexpr char kNtpPrimary[] = "pool.ntp.org";
constexpr char kNtpSecondary[] = "time.nist.gov";

// The zone as last set, so the SNTP start below can carry it and a zone set
// before the first sync is not lost.
char g_posixTz[64] = "";

bool g_sntpStarted = false;

}  // namespace

bool TimeSource::sync() {
  lastAttemptMs_ = millis();
  attempted_ = true;

  if (!g_sntpStarted) {
    // Safe to call before the link is up: the client keeps retrying until DNS
    // and a route exist.
    configTzTime(g_posixTz, kNtpPrimary, kNtpSecondary);
    g_sntpStarted = true;
  }

  const auto epoch = static_cast<uint32_t>(time(nullptr));
  if (epoch < kPlausibleEpochFloor) return false;

  valid_ = true;
  return true;
}

void TimeSource::maintain() {
  // Once the system clock is set the SNTP client keeps it there on its own
  // schedule, so there is nothing left for the loop to drive.
  if (valid_) return;
  if (attempted_ && millis() - lastAttemptMs_ < kRecheckIntervalMs) return;

  if (sync()) {
    Serial.print(F("[time] synced: "));
    Serial.println(now());
  }
}

uint32_t TimeSource::now() const {
  if (!valid_) return 0;
  return static_cast<uint32_t>(time(nullptr));
}

bool TimeSource::localNow(std::tm &out) const {
  if (!valid_) return false;

  // With no zone set TZ is empty, which newlib reads as UTC -- the same "show
  // something now, correct it once configured" behaviour the M4 has while its
  // offset lookup is still pending.
  const auto seconds = static_cast<std::time_t>(now());
  return localtime_r(&seconds, &out) != nullptr;
}

void TimeSource::setTz(const char *posixTz) {
  if (posixTz == nullptr) return;

  strncpy(g_posixTz, posixTz, sizeof(g_posixTz) - 1);
  g_posixTz[sizeof(g_posixTz) - 1] = '\0';

  setenv("TZ", g_posixTz, 1);
  tzset();

  Serial.print(F("[time] zone: "));
  Serial.println(g_posixTz[0] != '\0' ? g_posixTz : "UTC");
}
