#include "http_request.h"

#include <Arduino.h>
#include <cstring>

namespace {

constexpr uint32_t kReadTimeoutMs = 4000;
constexpr size_t kLineCap = 256;
constexpr uint16_t kMaxHeaders = 40;

enum LineStatus { kLineOk, kLineTruncated, kLineTimeout };

// Reads one CRLF-terminated line. Oversized lines are consumed to the end of the
// line so the parser stays in sync, and reported as truncated.
LineStatus readLine(WiFiClient &client, char *out, size_t cap, size_t &length) {
  size_t n = 0;
  bool overflowed = false;
  const uint32_t start = millis();

  while (millis() - start < kReadTimeoutMs) {
    const int c = client.read();
    if (c < 0) {
      if (!client.connected() && client.available() == 0) break;
      delay(1);
      continue;
    }

    if (c == '\n') {
      while (n > 0 && out[n - 1] == '\r') --n;
      out[n] = '\0';
      length = n;
      return overflowed ? kLineTruncated : kLineOk;
    }

    if (n + 1 < cap) {
      out[n++] = static_cast<char>(c);
    } else {
      overflowed = true;
    }
  }

  out[0] = '\0';
  length = 0;
  return kLineTimeout;
}

bool equalsIgnoringCase(const char *a, const char *b, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

// Splits "Name: value" and returns the value with leading whitespace removed.
const char *headerValue(const char *line, const char *name) {
  const size_t nameLen = strlen(name);
  if (strlen(line) < nameLen + 1) return nullptr;
  if (!equalsIgnoringCase(line, name, nameLen)) return nullptr;
  if (line[nameLen] != ':') return nullptr;

  const char *value = line + nameLen + 1;
  while (*value == ' ' || *value == '\t') ++value;
  return value;
}

bool copyCapped(const char *src, size_t srcLen, char *dest, size_t cap) {
  if (srcLen >= cap) return false;
  memcpy(dest, src, srcLen);
  dest[srcLen] = '\0';
  return true;
}

// Drains whatever is left so the client sees a clean close rather than a reset.
void drain(WiFiClient &client) {
  const uint32_t start = millis();
  while (client.available() > 0 && millis() - start < 200) {
    client.read();
  }
}

}  // namespace

HttpReadStatus readHttpRequest(WiFiClient &client, HttpRequest &request) {
  memset(&request, 0, sizeof(request));

  char line[kLineCap];
  size_t lineLen = 0;

  const LineStatus requestLine = readLine(client, line, sizeof(line), lineLen);
  if (requestLine == kLineTimeout) return kHttpTimeout;
  if (requestLine == kLineTruncated) return kHttpTargetTooLong;

  // "METHOD SP TARGET SP HTTP/1.1"
  const char *methodEnd = strchr(line, ' ');
  if (methodEnd == nullptr) return kHttpMalformed;
  if (!copyCapped(line, static_cast<size_t>(methodEnd - line), request.method,
                  sizeof(request.method))) {
    return kHttpMalformed;
  }

  const char *targetBegin = methodEnd + 1;
  const char *targetEnd = strchr(targetBegin, ' ');
  if (targetEnd == nullptr) return kHttpMalformed;
  if (!copyCapped(targetBegin, static_cast<size_t>(targetEnd - targetBegin), request.target,
                  sizeof(request.target))) {
    return kHttpTargetTooLong;
  }

  const char *query = strchr(request.target, '?');
  const size_t pathLen =
      query != nullptr ? static_cast<size_t>(query - request.target) : strlen(request.target);
  memcpy(request.path, request.target, pathLen);
  request.path[pathLen] = '\0';

  bool authorizationTruncated = false;
  uint16_t headerCount = 0;

  while (true) {
    if (++headerCount > kMaxHeaders) return kHttpHeaderTooLong;

    const LineStatus status = readLine(client, line, sizeof(line), lineLen);
    if (status == kLineTimeout) return kHttpTimeout;
    if (lineLen == 0 && status == kLineOk) break;  // end of headers

    if (status == kLineTruncated) {
      // Only Authorization is long enough to plausibly overflow, and silently
      // using a truncated one would turn a client bug into a signature failure.
      if (headerValue(line, "authorization") != nullptr) authorizationTruncated = true;
      continue;
    }

    const char *value = headerValue(line, "authorization");
    if (value != nullptr) {
      if (!copyCapped(value, strlen(value), request.authorization,
                      sizeof(request.authorization))) {
        authorizationTruncated = true;
      }
      continue;
    }

    value = headerValue(line, "content-length");
    if (value != nullptr) {
      uint32_t parsed = 0;
      for (const char *p = value; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return kHttpMalformed;
        parsed = parsed * 10 + static_cast<uint32_t>(*p - '0');
        if (parsed > 1000000U) return kHttpBodyTooLarge;
      }
      request.contentLength = parsed;
    }
  }

  if (authorizationTruncated) return kHttpHeaderTooLong;
  if (request.contentLength > HttpRequest::kBodyCap) return kHttpBodyTooLarge;

  const uint32_t start = millis();
  while (request.bodyLen < request.contentLength) {
    const int c = client.read();
    if (c < 0) {
      if (millis() - start >= kReadTimeoutMs) return kHttpTimeout;
      if (!client.connected() && client.available() == 0) return kHttpTimeout;
      delay(1);
      continue;
    }
    request.body[request.bodyLen++] = static_cast<char>(c);
  }
  request.body[request.bodyLen] = '\0';

  drain(client);
  return kHttpOk;
}

void sendJsonResponse(WiFiClient &client, int statusCode, const char *statusText,
                      const char *json) {
  client.print(F("HTTP/1.1 "));
  client.print(statusCode);
  client.print(' ');
  client.println(statusText);
  client.println(F("Content-Type: application/json"));
  client.print(F("Content-Length: "));
  client.println(strlen(json));
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();
  client.print(json);
}

void sendErrorResponse(WiFiClient &client, int statusCode, const char *statusText,
                       const char *errorCode) {
  char json[96];
  snprintf(json, sizeof(json), R"({"error":"%s"})", errorCode);
  sendJsonResponse(client, statusCode, statusText, json);
}
