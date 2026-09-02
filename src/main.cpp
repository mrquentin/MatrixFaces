#include <Adafruit_Protomatter.h>
#include <Client.h>

#include <cstring>

#include "api/api_context.h"
#include "api/handlers.h"
#include "api/http_request.h"
#include "apps/app_settings_store.h"
// Unqualified on purpose: -Isrc/board/<target> decides whose pin table this
// is, which is the rule that keeps the board out of the composition root.
#include "board_pins.h"

#include "board/button.h"
#include "board/metrics.h"
#include "board/net_link.h"
#include "board/rtos.h"
#include "board/secure_random.h"
#include "variants/registry.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

namespace {

constexpr uint32_t kFactoryResetHoldMs = 5000;
constexpr uint32_t kPairingBlinkMs = 150;

CredentialStore credentials;
TimeSource clockSource;
Authenticator authenticator(credentials, clockSource);
PairingWindow pairing;
Button buttonUp;
Button buttonDown;

// Single chain, double-buffered: TextApp's scroll animation redraws
// continuously, and every app already does a full fillScreen() each frame, so
// there's no stale-buffer content to worry about. Pin tables, geometry and bit
// depth all come from the board -- the last of those because how many
// bitplanes a board can drive without visible artefacts is a property of its
// refresh engine, not of the apps.
Adafruit_Protomatter matrix(board_pins::kMatrixWidth, board_pins::kMatrixBitDepth, 1,
                            board_pins::kMatrixRgb, board_pins::kMatrixAddrPins,
                            board_pins::kMatrixAddr, board_pins::kMatrixClock,
                            board_pins::kMatrixLatch, board_pins::kMatrixOe, true);
AppScheduler appScheduler(matrix);
AppSettingsStore appSettingsStore;

bool desiredLedState = false;

// Set when a command changed a setting, cleared once it has been written back.
// The handler used to persist inline; it cannot any more, because at that
// point the new value is still in the queue. Phase 5.2 replaces this with a
// debounce so a burst of changes costs one write instead of several.
bool settingsDirty = false;

// The API layer's whole view of the firmware. Assembled once here; handlers
// reach for nothing else.
// Which apps exist is the variant's decision; it also tells us whether there
// is a MultiViewer poll to report on.
AppRegistry appRegistry{appScheduler, clockSource, nullptr};

ApiContext apiContext{credentials,  authenticator,   pairing,
                      clockSource,  appScheduler,    desiredLedState,
                      FIRMWARE_VERSION, nullptr};

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

// Applies everything waiting in the command queue.
//
// This is the only place app state is written. Handlers validate and post;
// this drains and applies. While there is one task that distinction is
// invisible, which is exactly why it is worth establishing now -- phase 4.2
// puts the poster and this drain on different cores, and by then the rule has
// to already be true rather than newly imposed. See docs/concurrency.md.
void drainCommands() {
  Command command{};
  while (rtos::commandTake(command)) {
    switch (command.kind) {
      case Command::Kind::kSwitchApp:
        appScheduler.switchTo(command.appIndex);
        Serial.print(F("[apps] switched to "));
        Serial.println(appScheduler.activeName());
        break;

      case Command::Kind::kApplySetting:
        // Validated by the poster against the same bag, so a refusal here
        // means validate and apply disagree -- a bug worth hearing about
        // rather than a bad request.
        if (appScheduler.applySetting(command.appIndex, command.key, command.value)) {
          settingsDirty = true;
        } else {
          Serial.print(F("[apps] setting rejected after validation: "));
          Serial.println(command.key);
        }
        break;

      case Command::Kind::kSetLed:
        desiredLedState = command.on;
        break;
    }
  }

  if (settingsDirty) {
    appSettingsStore.saveAll(appScheduler);
    settingsDirty = false;
  }
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

  // First, because on a board that measures its stack by painting the RAM
  // above it, this has to happen before anything makes that stack deep.
  metrics::begin();

  secure_random::begin();
  credentials.begin();
  buttonUp.begin(board_pins::kButtonUp);
  buttonDown.begin(board_pins::kButtonDown);

  // Bring up the RGB matrix before anything WiFi-related blocks, so the
  // clock app can render while the board is still negotiating a connection.
  registerApps(appRegistry);
  apiContext.mvCounters = appRegistry.mvCounters;
  appSettingsStore.begin(appScheduler);
  const ProtomatterStatus matrixStatus = appScheduler.begin();
  if (matrixStatus != PROTOMATTER_OK) {
    Serial.print(F("Protomatter begin() failed, status="));
    Serial.println(static_cast<int>(matrixStatus));
    haltBlinking(F("Halting: matrix init failed"));
  }

  Serial.print(F("MatrixFaces firmware: "));
  Serial.println(FIRMWARE_VERSION);

  net_link::begin();

  if (!clockSource.sync()) {
    Serial.println(F("[time] no NTP time yet; API calls return 503 until it syncs"));
  }

  net_link::serveHttp(80);
  Serial.println(F("Press UP to open a 60s pairing window; hold DOWN 5s to revoke all clients."));
}

void loop() {
  metrics::markLoop();

  // First, so a mutation posted by the request served at the end of the
  // previous pass is in effect before anything reads app state this pass.
  drainCommands();

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

  net_link::maintain();

  clockSource.maintain();
  appScheduler.update(millis());
  updateLed();
  metrics::tick();
  reportMetrics();

  Client *client = net_link::accept();
  if (client == nullptr) return;

  // Timed around the work only: the flush finishRequest() does is a fixed cost
  // that would otherwise swamp the numbers it is meant to expose. Cycles rather
  // than micros() so the whole thing folds away when metrics are compiled out.
  const uint32_t requestStartCycles = metrics::cycles();
  serveClient(*client);
  metrics::recordRequest(metrics::cycles() - requestStartCycles);

  net_link::finishRequest();
}
