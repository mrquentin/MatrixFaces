#include <Arduino.h>
#include <WiFiNINA.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "board/samd51/local_time.h"
#include "board/time_source.h"
#include "net/stream_read.h"

// Local-time offset for the M4, resolved from the board's public IP via a
// geolocation lookup (ip-api.com) and refreshed periodically so DST
// transitions self-correct without any hardcoded date math.
//
// This is the whole of what used to be net/timezone_offset: an ip-api.com
// dependency has no business sitting in shared code, since the S3 gets its
// zone from SNTP and a POSIX TZ string instead and never makes this request.
// The privacy cost -- the board's public IP reaching a third party every 12
// hours -- is documented in docs/security.md and disappears with the S3.
namespace {

constexpr char kHost[] = "ip-api.com";
constexpr uint16_t kPort = 80;
constexpr uint32_t kResponseTimeoutMs = 5000;
// Headers plus the trimmed `fields=status,offset` body comfortably fit; this
// leaves generous margin without holding a page-sized buffer on the stack.
constexpr size_t kResponseCap = 512;

constexpr uint32_t kResyncIntervalMs = 43200000;  // 12h: DST changes twice a year
constexpr uint32_t kRetryIntervalMs = 60000;      // until the first success

// The lookup's own socket. Unlike the transports the composition root owns,
// this one never crosses into shared code -- it exists only because *this
// board* answers "what time is it locally?" with an HTTP request, which is
// precisely the kind of detail src/board/samd51 is for.
WiFiClient g_transport;

int32_t g_offsetSeconds = 0;
bool g_valid = false;
uint32_t g_lastAttemptMs = 0;
bool g_attempted = false;

bool resolve() {
  if (!g_transport.connect(kHost, kPort)) return false;

  g_transport.print(F("GET /json/?fields=status,offset HTTP/1.1\r\n"
                      "Host: ip-api.com\r\n"
                      "Connection: close\r\n\r\n"));

  char response[kResponseCap];
  net::readUntilClose(g_transport, response, sizeof(response), kResponseTimeoutMs);
  g_transport.stop();

  const char *body = strstr(response, "\r\n\r\n");
  if (body == nullptr) return false;
  body += 4;

  if (strstr(body, R"("status":"success")") == nullptr) return false;

  const char *offsetKey = strstr(body, R"("offset")");
  if (offsetKey == nullptr) return false;
  const char *colon = strchr(offsetKey, ':');
  if (colon == nullptr) return false;

  char *end = nullptr;
  const long value = strtol(colon + 1, &end, 10);
  if (end == colon + 1) return false;

  g_offsetSeconds = static_cast<int32_t>(value);
  return true;
}

}  // namespace

namespace samd51_local_time {

// Called from TimeSource::maintain(), which is already throttling itself for
// the clock; this keeps its own, much longer, schedule.
void maintain() {
  const uint32_t interval = g_valid ? kResyncIntervalMs : kRetryIntervalMs;
  if (g_attempted && millis() - g_lastAttemptMs < interval) return;

  g_lastAttemptMs = millis();
  g_attempted = true;
  if (resolve()) g_valid = true;
}

int32_t offsetSeconds() { return g_offsetSeconds; }

}  // namespace samd51_local_time

bool TimeSource::localNow(std::tm &out) const {
  if (!valid_) return false;

  // Shift into local time and then break it down as if it were UTC, which is
  // what gmtime_r does without consulting any zone database.
  const auto local = static_cast<std::time_t>(static_cast<int64_t>(now()) +
                                              samd51_local_time::offsetSeconds());
  return gmtime_r(&local, &out) != nullptr;
}

void TimeSource::setTz(const char *posixTz) {
  // This board resolves its offset by geolocation; see board_caps::kHasPosixTz.
  (void)posixTz;
}
