#include <Adafruit_Protomatter.h>
#include <SPI.h>
#include <WiFiNINA.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "api/authenticator.h"
#include "api/credential_store.h"
#include "api/http_request.h"
#include "api/pairing_window.h"
#include "apps/app.h"
#include "apps/app_scheduler.h"
#include "apps/clock_app.h"
#include "board/button.h"
#include "board/metrics.h"
#include "board/secure_random.h"
#include "board/time_source.h"
#include "hex.h"
#include "secrets.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

namespace {

// The Matrix Portal M4's UP/DOWN buttons; see the variant's pin table.
constexpr uint8_t kButtonUpPin = 2;
constexpr uint8_t kButtonDownPin = 3;
constexpr uint32_t kFactoryResetHoldMs = 5000;
constexpr uint32_t kPairingBlinkMs = 150;

WiFiServer server(80);

CredentialStore credentials;
TimeSource clockSource;
Authenticator authenticator(credentials, clockSource);
PairingWindow pairing;
Button buttonUp;
Button buttonDown;

// RGB matrix wiring for a 128x64 panel; see
// https://learn.adafruit.com/adafruit-matrixportal-m4/protomatter-arduino-library
uint8_t matrixRgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t matrixAddrPins[] = {17, 18, 19, 20, 21};
constexpr uint8_t kMatrixClockPin = 14;
constexpr uint8_t kMatrixLatchPin = 15;
constexpr uint8_t kMatrixOePin = 16;

// Bit depth 4, single chain, single-buffered: nothing drawn yet animates fast
// enough to need tear-free double buffering. Height is inferred from the
// address-pin count (5 pins -> 2*2^5 = 64px), not passed explicitly.
Adafruit_Protomatter matrix(128, 4, 1, matrixRgbPins, 5, matrixAddrPins, kMatrixClockPin,
                            kMatrixLatchPin, kMatrixOePin, false);
AppScheduler appScheduler(matrix);
ClockApp clockApp(clockSource);

bool desiredLedState = false;

void printWiFiStatus() {
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());
  // noinspection HttpUrlsUsage  -- the board serves plain HTTP by design
  Serial.print(F("IP address: http://"));
  Serial.println(WiFi.localIP());
  Serial.print(F("Signal strength (RSSI): "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

void connectWiFi() {
  do {
    Serial.print(F("Connecting to "));
    Serial.println(SECRET_SSID);
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(2000);
  } while (WiFi.status() != WL_CONNECTED);

  Serial.println(F("Connected!"));
  printWiFiStatus();
}

// Blinks the LED forever after printing a fatal boot error; never returns.
[[noreturn]] void haltBlinking(const __FlashStringHelper *message) {
  Serial.println(message);
  while (true) {
    digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) == LOW ? HIGH : LOW);
    delay(100);
  }
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

// Unauthenticated discovery endpoint. Exposes only what a client needs to know
// before it has credentials.
void handleRoot(WiFiClient &client) {
  char json[224];
  snprintf(json, sizeof(json),
           R"({"device":"matrixfaces","firmware_version":"%s","paired_clients":%u,)"
           R"("pairing_open":%s,"pairing_expires_in":%lu,"clock_synced":%s})",
           FIRMWARE_VERSION, static_cast<unsigned>(credentials.count()),
           pairing.isOpen() ? "true" : "false",
           static_cast<unsigned long>(pairing.remainingSeconds()),
           clockSource.isValid() ? "true" : "false");
  sendJsonResponse(client, 200, "OK", json);
}

void handlePair(WiFiClient &client) {
  if (!pairing.isOpen()) {
    // Deliberately the same response whether the window never opened or has
    // already expired.
    sendErrorResponse(client, 401, "Unauthorized", "pairing_closed");
    return;
  }
  if (credentials.full()) {
    sendErrorResponse(client, 409, "Conflict", "too_many_clients");
    return;
  }

  StoredClient stored{};

  // A collision across 8 random bytes is vanishingly unlikely, but retrying is
  // cheaper than reasoning about what a duplicate id would do to lookups.
  bool generated = false;
  for (int attempt = 0; attempt < 4 && !generated; ++attempt) {
    if (!secure_random::bytes(stored.id, sizeof(stored.id))) break;
    if (credentials.find(stored.id) == nullptr) generated = true;
  }
  if (!generated || !secure_random::bytes(stored.secret, sizeof(stored.secret))) {
    Serial.println(F("[pair] TRNG unavailable, refusing to issue credentials"));
    sendErrorResponse(client, 500, "Internal Server Error", "rng_unavailable");
    return;
  }

  stored.pairedAt = clockSource.isValid() ? clockSource.now() : 0;

  if (!credentials.add(stored)) {
    sendErrorResponse(client, 409, "Conflict", "too_many_clients");
    return;
  }

  // One successful pairing closes the window, as does the 60 second timeout.
  pairing.close();

  char idHex[apiauth::kClientIdHexLen + 1];
  char secretHex[apiauth::kSecretHexLen + 1];
  apiauth::toHex(stored.id, sizeof(stored.id), idHex);
  apiauth::toHex(stored.secret, sizeof(stored.secret), secretHex);

  char json[192];
  snprintf(json, sizeof(json), R"({"client_id":"%s","secret":"%s","algorithm":"HMAC-SHA256"})",
           idHex, secretHex);
  sendJsonResponse(client, 200, "OK", json);

  // The secret is intentionally never logged; this is its only appearance.
  Serial.print(F("[pair] issued credentials to client "));
  Serial.println(idHex);

  memset(secretHex, 0, sizeof(secretHex));
}

void handleStatus(WiFiClient &client) {
  char json[192];
  snprintf(json, sizeof(json),
           R"({"uptime_s":%lu,"led":%s,"rssi":%ld,"paired_clients":%u,"time":%lu})",
           static_cast<unsigned long>(millis() / 1000), desiredLedState ? "true" : "false",
           static_cast<long>(WiFi.RSSI()), static_cast<unsigned>(credentials.count()),
           static_cast<unsigned long>(clockSource.isValid() ? clockSource.now() : 0));
  sendJsonResponse(client, 200, "OK", json);
}

// Minimal extraction of a boolean member; the payloads here are a single key.
bool extractJsonBool(const char *json, const char *key, bool &out) {
  char pattern[24];
  snprintf(pattern, sizeof(pattern), R"("%s")", key);

  const char *found = strstr(json, pattern);
  if (found == nullptr) return false;

  const char *p = found + strlen(pattern);
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != ':') return false;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;

  if (strncmp(p, "true", 4) == 0) {
    out = true;
    return true;
  }
  if (strncmp(p, "false", 5) == 0) {
    out = false;
    return true;
  }
  return false;
}

// Minimal extraction of an unsigned integer member; the payloads here are a
// single key, and rejects anything with a stray decimal point or sign so a
// float or negative number doesn't silently truncate into a valid index.
bool extractJsonUInt(const char *json, const char *key, uint32_t &out) {
  char pattern[24];
  snprintf(pattern, sizeof(pattern), R"("%s")", key);

  const char *found = strstr(json, pattern);
  if (found == nullptr) return false;

  const char *p = found + strlen(pattern);
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != ':') return false;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p < '0' || *p > '9') return false;

  char *end = nullptr;
  const unsigned long value = strtoul(p, &end, 10);
  if (end == p || *end == '.') return false;

  out = static_cast<uint32_t>(value);
  return true;
}

void handleSetLed(WiFiClient &client, const HttpRequest &request) {
  bool on = false;
  if (!extractJsonBool(request.body, "on", on)) {
    sendErrorResponse(client, 400, "Bad Request", "expected_on_boolean");
    return;
  }

  desiredLedState = on;

  char json[48];
  snprintf(json, sizeof(json), R"({"led":%s})", on ? "true" : "false");
  sendJsonResponse(client, 200, "OK", json);
}

// snprintf reports what it *would* have written, so the running offset is
// clamped rather than added blindly; otherwise sizeof(json) - offset could
// underflow once the buffer filled.
// Responses are printf-formatted; a parameter pack would buy nothing here.
// NOLINTNEXTLINE(cert-dcl50-cpp,cppcoreguidelines-pro-type-vararg)
void appendJson(char *json, size_t cap, size_t &offset, const char *format, ...) {
  if (offset >= cap) return;

  va_list args;
  va_start(args, format);
  const int written = vsnprintf(json + offset, cap - offset, format, args);
  va_end(args);

  if (written < 0) return;
  offset += static_cast<size_t>(written);
  if (offset >= cap) offset = cap - 1;
}

void handleListClients(WiFiClient &client) {
  char json[320];
  size_t offset = 0;
  appendJson(json, sizeof(json), offset, R"({"clients":[)");

  for (uint8_t i = 0; i < credentials.count(); ++i) {
    const StoredClient &stored = credentials.at(i);
    char idHex[apiauth::kClientIdHexLen + 1];
    apiauth::toHex(stored.id, sizeof(stored.id), idHex);

    appendJson(json, sizeof(json), offset, R"(%s{"id":"%s","paired_at":%lu})", i == 0 ? "" : ",",
               idHex, static_cast<unsigned long>(stored.pairedAt));
  }

  appendJson(json, sizeof(json), offset, "]}");
  sendJsonResponse(client, 200, "OK", json);
}

void handleGetActiveApp(WiFiClient &client) {
  char json[80];
  snprintf(json, sizeof(json), R"({"index":%u,"name":"%s"})",
           static_cast<unsigned>(appScheduler.activeIndex()), appScheduler.activeName());
  sendJsonResponse(client, 200, "OK", json);
}

void handleListApps(WiFiClient &client) {
  char json[256];
  size_t offset = 0;
  appendJson(json, sizeof(json), offset, R"({"apps":[)");

  for (uint8_t i = 0; i < appScheduler.count(); ++i) {
    appendJson(json, sizeof(json), offset, R"(%s{"index":%u,"name":"%s"})", i == 0 ? "" : ",",
               static_cast<unsigned>(i), appScheduler.name(i));
  }

  appendJson(json, sizeof(json), offset, R"(],"active_index":%u,"active_name":"%s"})",
             static_cast<unsigned>(appScheduler.activeIndex()), appScheduler.activeName());
  sendJsonResponse(client, 200, "OK", json);
}

void handleSetActiveApp(WiFiClient &client, const HttpRequest &request) {
  uint32_t index = 0;
  if (!extractJsonUInt(request.body, "index", index)) {
    sendErrorResponse(client, 400, "Bad Request", "expected_index_integer");
    return;
  }
  if (index >= appScheduler.count()) {
    sendErrorResponse(client, 400, "Bad Request", "unknown_app_index");
    return;
  }

  appScheduler.switchTo(static_cast<uint8_t>(index));
  Serial.print(F("[apps] switched to "));
  Serial.println(appScheduler.activeName());
  handleGetActiveApp(client);
}

#if METRICS_ENABLED
void handleMetrics(WiFiClient &client) {
  const metrics::Snapshot m = metrics::snapshot();

  char json[416];
  size_t offset = 0;
  appendJson(json, sizeof(json), offset,
             R"({"cpu":{"loop_hz":%lu,"busy_permille":%lu,"requests":%lu,)"
             R"("req_avg_us":%lu,"req_max_us":%lu,"auth_avg_us":%lu,"auth_max_us":%lu},)",
             static_cast<unsigned long>(m.loopHz), static_cast<unsigned long>(m.busyPermille),
             static_cast<unsigned long>(m.requests),
             static_cast<unsigned long>(m.requestAvgMicros),
             static_cast<unsigned long>(m.requestMaxMicros),
             static_cast<unsigned long>(m.authAvgMicros),
             static_cast<unsigned long>(m.authMaxMicros));
  appendJson(json, sizeof(json), offset,
             R"("ram":{"total":%lu,"static":%lu,"heap_used":%lu,"stack_peak":%lu,)"
             R"("free_now":%lu,"min_free_ever":%lu}})",
             static_cast<unsigned long>(m.ramTotal), static_cast<unsigned long>(m.ramStatic),
             static_cast<unsigned long>(m.heapUsed), static_cast<unsigned long>(m.stackPeak),
             static_cast<unsigned long>(m.freeNow), static_cast<unsigned long>(m.minFreeEver));

  sendJsonResponse(client, 200, "OK", json);
}
#endif  // METRICS_ENABLED

void handleRevokeClient(WiFiClient &client, const char *idHex) {
  uint8_t id[apiauth::kClientIdBytes];
  if (strlen(idHex) != apiauth::kClientIdHexLen ||
      !apiauth::fromHex(idHex, apiauth::kClientIdHexLen, id, sizeof(id))) {
    sendErrorResponse(client, 400, "Bad Request", "invalid_client_id");
    return;
  }

  if (!credentials.remove(id)) {
    sendErrorResponse(client, 404, "Not Found", "unknown_client");
    return;
  }

  authenticator.forget(id);
  Serial.print(F("[creds] revoked client "));
  Serial.println(idHex);
  sendJsonResponse(client, 200, "OK", "{\"revoked\":true}");
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

bool methodIs(const HttpRequest &request, const char *method) {
  return strcmp(request.method, method) == 0;
}

void routeAuthenticated(WiFiClient &client, const HttpRequest &request) {
  if (methodIs(request, "GET") && strcmp(request.path, "/api/status") == 0) {
    handleStatus(client);
    return;
  }
  if (methodIs(request, "POST") && strcmp(request.path, "/api/led") == 0) {
    handleSetLed(client, request);
    return;
  }
  if (methodIs(request, "GET") && strcmp(request.path, "/api/clients") == 0) {
    handleListClients(client);
    return;
  }

  if (methodIs(request, "GET") && strcmp(request.path, "/api/apps") == 0) {
    handleListApps(client);
    return;
  }
  if (methodIs(request, "GET") && strcmp(request.path, "/api/app") == 0) {
    handleGetActiveApp(client);
    return;
  }
  if (methodIs(request, "POST") && strcmp(request.path, "/api/app") == 0) {
    handleSetActiveApp(client, request);
    return;
  }

#if METRICS_ENABLED
  if (methodIs(request, "GET") && strcmp(request.path, "/api/metrics") == 0) {
    handleMetrics(client);
    return;
  }
#endif

  constexpr char kClientsPrefix[] = "/api/clients/";
  if (methodIs(request, "DELETE") &&
      strncmp(request.path, kClientsPrefix, sizeof(kClientsPrefix) - 1) == 0) {
    handleRevokeClient(client, request.path + sizeof(kClientsPrefix) - 1);
    return;
  }

  sendErrorResponse(client, 404, "Not Found", "unknown_endpoint");
}

void routeRequest(WiFiClient &client, const HttpRequest &request) {
  if (methodIs(request, "GET") && strcmp(request.path, "/") == 0) {
    handleRoot(client);
    return;
  }
  if (methodIs(request, "POST") && strcmp(request.path, "/pair") == 0) {
    handlePair(client);
    return;
  }

  // Everything under /api requires a valid signature. Note that the signature
  // covers request.target, query string included, not the routing path.
  if (strncmp(request.path, "/api/", 5) != 0) {
    sendErrorResponse(client, 404, "Not Found", "unknown_endpoint");
    return;
  }

  uint8_t clientId[apiauth::kClientIdBytes];
  const uint32_t authStartCycles = metrics::cycles();
  const AuthResult result = authenticator.authenticate(
      request.authorization, request.method, request.target, request.body, request.bodyLen,
      clientId);
  metrics::recordAuth(metrics::cycles() - authStartCycles);

  if (result != kAuthOk) {
    Serial.print(F("[auth] rejected "));
    Serial.print(request.method);
    Serial.print(' ');
    Serial.print(request.path);
    Serial.print(F(" -> "));
    Serial.println(authResultCode(result));

    if (result == kAuthClockUnavailable) {
      sendErrorResponse(client, 503, "Service Unavailable", authResultCode(result));
    } else {
      sendErrorResponse(client, 401, "Unauthorized", authResultCode(result));
    }
    return;
  }

  routeAuthenticated(client, request);
}

void serveClient(WiFiClient &client) {
  HttpRequest request{};
  const HttpReadStatus status = readHttpRequest(client, request);

  switch (status) {
    case kHttpOk:
      routeRequest(client, request);
      break;
    case kHttpTimeout:
      sendErrorResponse(client, 408, "Request Timeout", "request_timeout");
      break;
    case kHttpMalformed:
      sendErrorResponse(client, 400, "Bad Request", "malformed_request");
      break;
    case kHttpTargetTooLong:
      sendErrorResponse(client, 414, "URI Too Long", "target_too_long");
      break;
    case kHttpHeaderTooLong:
      sendErrorResponse(client, 431, "Request Header Fields Too Large", "header_too_long");
      break;
    case kHttpBodyTooLarge:
      sendErrorResponse(client, 413, "Payload Too Large", "body_too_large");
      break;
  }

  // The secret material in a pairing response lives in `request` only for the
  // request body, but zeroing keeps stale Authorization headers out of RAM.
  memset(&request, 0, sizeof(request));
}

// ---------------------------------------------------------------------------
// Board feedback
// ---------------------------------------------------------------------------

void updateLed() {
  if (pairing.isOpen()) {
    digitalWrite(LED_BUILTIN, millis() / kPairingBlinkMs % 2 == 0 ? HIGH : LOW);
    return;
  }
  digitalWrite(LED_BUILTIN, desiredLedState ? HIGH : LOW);
}

#if METRICS_ENABLED
// Terse periodic line so the numbers are visible on a headless board too.
void reportMetrics() {
  static uint32_t lastReportMs = 0;
  if (millis() - lastReportMs < 30000) return;
  lastReportMs = millis();

  const metrics::Snapshot m = metrics::snapshot();
  Serial.print(F("[metrics] loop="));
  Serial.print(m.loopHz);
  Serial.print(F("Hz busy="));
  Serial.print(static_cast<float>(m.busyPermille) / 10.0F, 1);
  Serial.print(F("% reqs="));
  Serial.print(m.requests);
  Serial.print(F(" auth_avg="));
  Serial.print(m.authAvgMicros);
  Serial.print(F("us stack_peak="));
  Serial.print(m.stackPeak);
  Serial.print(F("B min_free="));
  Serial.print(m.minFreeEver);
  Serial.println(F("B"));
}
#else
void reportMetrics() {}
#endif  // METRICS_ENABLED

}  // namespace

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  // Wait up to 5s for the serial monitor, then carry on headless
  const unsigned long start = millis();
  while (!Serial && millis() - start < 5000) {}

  // Paints unused RAM, so it must run before anything makes the stack deep.
  metrics::begin();

  secure_random::begin();
  credentials.begin();
  buttonUp.begin(kButtonUpPin);
  buttonDown.begin(kButtonDownPin);

  // Bring up the RGB matrix before anything WiFi-related blocks, so the
  // clock app can render while the board is still negotiating a connection.
  appScheduler.add(clockApp);
  const ProtomatterStatus matrixStatus = appScheduler.begin();
  if (matrixStatus != PROTOMATTER_OK) {
    Serial.print(F("Protomatter begin() failed, status="));
    Serial.println(static_cast<int>(matrixStatus));
    haltBlinking(F("Halting: matrix init failed"));
  }

  // The Matrix Portal M4's ESP32 co-processor is on non-default pins
  WiFi.setPins(SPIWIFI_SS, SPIWIFI_ACK, ESP32_RESETN, ESP32_GPIO0, &SPIWIFI);

  if (WiFi.status() == WL_NO_MODULE) {
    haltBlinking(F("Communication with WiFi module failed!"));
  }

  Serial.print(F("MatrixFaces firmware: "));
  Serial.println(FIRMWARE_VERSION);
  Serial.print(F("WiFi module firmware: "));
  Serial.println(WiFiClass::firmwareVersion());

  connectWiFi();

  if (!clockSource.sync()) {
    Serial.println(F("[time] no NTP time yet; API calls return 503 until it syncs"));
  }

  server.begin();
  Serial.println(F("Press UP to open a 60s pairing window; hold DOWN 5s to revoke all clients."));
}

void loop() {
  metrics::markLoop();

  buttonUp.poll();
  buttonDown.poll();

  if (buttonUp.takePress()) {
    pairing.open();
    Serial.print(F("[pair] window open for "));
    Serial.print(PairingWindow::kWindowMs / 1000);
    Serial.println(F("s"));
  }

  if (buttonDown.takeHold(kFactoryResetHoldMs)) {
    if (credentials.clear()) {
      authenticator.forgetAll();
      Serial.println(F("[creds] all clients revoked"));
    } else {
      Serial.println(F("[creds] nothing to revoke"));
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi lost, reconnecting"));
    connectWiFi();
    server.begin();
  }

  clockSource.maintain();
  appScheduler.update(millis());
  updateLed();
  metrics::tick();
  reportMetrics();

  WiFiClient client = server.available();
  if (!client) return;

  // Timed around the work only: the flush delay below is a fixed cost that
  // would otherwise swamp the numbers it is meant to expose. Cycles rather than
  // micros() so the whole thing folds away when metrics are compiled out.
  const uint32_t requestStartCycles = metrics::cycles();
  serveClient(client);
  metrics::recordRequest(metrics::cycles() - requestStartCycles);

  delay(10);
  client.stop();
}
