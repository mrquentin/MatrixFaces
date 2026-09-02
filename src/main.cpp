#include <Adafruit_Protomatter.h>
#include <Client.h>
#include <SPI.h>
#include <WiFiNINA.h>

#include <cstring>

#include "api/api_context.h"
#include "api/handlers.h"
#include "api/http_request.h"
#include "apps/clock_app.h"
#include "apps/f1_flags_app.h"
#include "apps/flag_test_app.h"
#include "apps/text_app.h"
#include "board/button.h"
#include "board/metrics.h"
#include "board/secure_random.h"
#include "net/timezone_offset.h"
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

// Outbound sockets, one per consumer. WiFiClient reuses its socket across
// connect/stop cycles, so a single instance per consumer is equivalent to the
// short-lived locals these replace -- but two consumers sharing one instance
// would tear down each other's connections, hence one each. Owned here so that
// nothing under src/net or src/api has to know what the radio is.
WiFiClient timezoneTransport;
WiFiClient multiViewerTransport;

CredentialStore credentials;
TimeSource clockSource;
Authenticator authenticator(credentials, clockSource);
PairingWindow pairing;
Button buttonUp;
Button buttonDown;

// RGB matrix wiring for a 128x64 panel; see
// https://learn.adafruit.com/adafruit-matrixportal-m4/protomatter-arduino-library
//
// Pin order is R1,G1,B1,R2,G2,B2 by Protomatter convention, but this panel's
// physical HUB75 wiring cycles one position off that (confirmed by testing
// pure red/green/blue: software R lit the panel's B sub-pixel, G lit R, B lit
// G). Rotated left by one position per triplet so software "R" drives the
// pin actually wired to the panel's R input, etc.
uint8_t matrixRgbPins[] = {8, 9, 7, 11, 12, 10};
uint8_t matrixAddrPins[] = {17, 18, 19, 20, 21};
constexpr uint8_t kMatrixClockPin = 14;
constexpr uint8_t kMatrixLatchPin = 15;
constexpr uint8_t kMatrixOePin = 16;

// Bit depth 4, single chain, double-buffered: TextApp's scroll animation
// redraws continuously, and every app already does a full fillScreen() each
// frame, so there's no stale-buffer content to worry about. Height is
// inferred from the address-pin count (5 pins -> 2*2^5 = 64px), not passed
// explicitly.
Adafruit_Protomatter matrix(128, 4, 1, matrixRgbPins, 5, matrixAddrPins, kMatrixClockPin,
                            kMatrixLatchPin, kMatrixOePin, true);
AppScheduler appScheduler(matrix);
TimezoneOffset timezoneOffset(timezoneTransport);
ClockApp clockApp(clockSource, timezoneOffset);
TextApp textApp;
F1FlagsApp f1FlagsApp(multiViewerTransport);
FlagTestApp flagTestApp;
AppSettingsStore appSettingsStore;

bool desiredLedState = false;

int32_t currentRssi() { return WiFi.RSSI(); }

// The API layer's whole view of the firmware. Assembled once here; handlers
// reach for nothing else.
ApiContext apiContext{credentials,      authenticator,
                      pairing,          clockSource,
                      appScheduler,     appSettingsStore,
                      desiredLedState,  FIRMWARE_VERSION,
                      currentRssi};

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

void serveClient(Client &client) {
  HttpRequest request{};
  const HttpReadStatus status = readHttpRequest(client, request);

  switch (status) {
    case kHttpOk:
      api::handleRequest(client, request, apiContext);
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
  appScheduler.add(textApp);
  appScheduler.add(f1FlagsApp);
  appScheduler.add(flagTestApp);
  appSettingsStore.begin(appScheduler);
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
  timezoneOffset.maintain();
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
