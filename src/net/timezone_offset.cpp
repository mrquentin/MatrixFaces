#include "timezone_offset.h"

#include <Arduino.h>
#include <WiFiNINA.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr char kHost[] = "ip-api.com";
constexpr uint16_t kPort = 80;
constexpr uint32_t kResponseTimeoutMs = 5000;
// Headers plus the trimmed `fields=status,offset` body comfortably fit; this
// leaves generous margin without holding a page-sized buffer on the stack.
constexpr size_t kResponseCap = 512;

}  // namespace

bool TimezoneOffset::resolve() {
  WiFiClient client;
  if (!client.connect(kHost, kPort)) return false;

  client.print(F("GET /json/?fields=status,offset HTTP/1.1\r\n"
                  "Host: ip-api.com\r\n"
                  "Connection: close\r\n\r\n"));

  char response[kResponseCap];
  size_t len = 0;
  const uint32_t start = millis();
  while (len + 1 < sizeof(response)) {
    const int c = client.read();
    if (c < 0) {
      if (millis() - start >= kResponseTimeoutMs) break;
      if (!client.connected() && client.available() == 0) break;
      delay(1);
      continue;
    }
    response[len++] = static_cast<char>(c);
  }
  response[len] = '\0';
  client.stop();

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

  offsetSeconds_ = static_cast<int32_t>(value);
  return true;
}

void TimezoneOffset::maintain() {
  const uint32_t interval = valid_ ? kResyncIntervalMs : kRetryIntervalMs;
  if (attempted_ && millis() - lastAttemptMs_ < interval) return;

  lastAttemptMs_ = millis();
  attempted_ = true;
  if (resolve()) valid_ = true;
}
