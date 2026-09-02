#include "http_request.h"

#include <Arduino.h>
#include <cstring>

#include "net/stream_read.h"

namespace {

constexpr uint32_t kReadTimeoutMs = 4000;
constexpr size_t kLineCap = 256;
constexpr uint16_t kMaxHeaders = 40;
// Long enough to clear whatever the peer already sent, short enough that a
// client which keeps talking cannot stall the loop.
constexpr uint32_t kDrainTimeoutMs = 200;

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

}  // namespace

HttpReadStatus readHttpRequest(Client &client, HttpRequest &request) {
  memset(&request, 0, sizeof(request));

  char line[kLineCap];
  size_t lineLen = 0;

  const net::LineStatus requestLine =
      net::readLine(client, line, sizeof(line), lineLen, kReadTimeoutMs);
  if (requestLine == net::LineStatus::kTimeout) return kHttpTimeout;
  if (requestLine == net::LineStatus::kTruncated) return kHttpTargetTooLong;

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

    const net::LineStatus status =
        net::readLine(client, line, sizeof(line), lineLen, kReadTimeoutMs);
    if (status == net::LineStatus::kTimeout) return kHttpTimeout;
    if (lineLen == 0 && status == net::LineStatus::kOk) break;  // end of headers

    if (status == net::LineStatus::kTruncated) {
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

    value = headerValue(line, "sec-websocket-key");
    if (value != nullptr) {
      // A key that does not fit is left empty rather than truncated: the
      // handshake would fail anyway, and an empty one is how the route says
      // "this was not a WebSocket request" -- which is the honest answer.
      copyCapped(value, strlen(value), request.websocketKey, sizeof(request.websocketKey));
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

  if (!net::readExactly(client, request.body, request.contentLength, kReadTimeoutMs)) {
    return kHttpTimeout;
  }
  request.bodyLen = request.contentLength;
  request.body[request.bodyLen] = '\0';

  net::drainBuffered(client, kDrainTimeoutMs);
  return kHttpOk;
}

void sendJsonResponse(Client &client, int statusCode, const char *statusText, const char *json) {
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

void sendErrorResponse(Client &client, int statusCode, const char *statusText,
                       const char *errorCode) {
  char json[96];
  snprintf(json, sizeof(json), R"({"error":"%s"})", errorCode);
  sendJsonResponse(client, statusCode, statusText, json);
}
