#pragma once

#include <WiFiNINA.h>

#include <cstddef>
#include <cstdint>

// A parsed request, held in fixed buffers so a hostile client cannot drive the
// heap on a board with 192 KB of RAM.
struct HttpRequest {
  static constexpr size_t kMethodCap = 8;
  static constexpr size_t kTargetCap = 96;
  static constexpr size_t kAuthorizationCap = 192;
  static constexpr size_t kBodyCap = 256;

  char method[kMethodCap];
  // The request target exactly as sent, query string included. This is what the
  // signature covers, so it must not be normalised.
  char target[kTargetCap];
  // `target` truncated at '?', used for routing only.
  char path[kTargetCap];
  char authorization[kAuthorizationCap];
  char body[kBodyCap + 1];
  size_t bodyLen;
  uint32_t contentLength;
};

enum HttpReadStatus {
  kHttpOk,
  kHttpTimeout,
  kHttpMalformed,
  kHttpTargetTooLong,
  kHttpHeaderTooLong,
  kHttpBodyTooLarge,
};

// Reads a full request from `client`. Returns the first failure encountered;
// the caller is responsible for sending an error response.
HttpReadStatus readHttpRequest(WiFiClient &client, HttpRequest &request);

void sendJsonResponse(WiFiClient &client, int statusCode, const char *statusText,
                      const char *json);

// Sends {"error":"<code>"} with the given status.
void sendErrorResponse(WiFiClient &client, int statusCode, const char *statusText,
                       const char *errorCode);
