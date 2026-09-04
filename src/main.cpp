#include <ArduinoJson.h>
#include <Client.h>

#include <atomic>
#include <cstring>

#include "api/api_context.h"
#include "api/handlers.h"
#include "api/http_request.h"
#include "api/ws_hub.h"
#include "api/ws_ticket.h"
#include "apps/app_settings_store.h"
#include "storage/quiet_timer.h"
#include "board_pins.h"
#include "matrix_gfx.h"

#include "board/button.h"
#include "board/exec.h"
#include "board/fs.h"
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
MatrixGfx &matrix = matrixInstance();
AppScheduler appScheduler(matrix);
AppSettingsStore appSettingsStore;

// Raised by housekeeping when DOWN is held, acted on by the network task.
//
// CredentialStore and Authenticator are the network task's alone: it pairs, it
// lists, it revokes, and it authenticates every request against them. The
// factory reset used to write them straight from the button handler, which was
// harmless while that was the same task and is a data race now that it is not.
// A flag hands the work to the owner instead, which needs no lock at all.
std::atomic<bool> revokeAllRequested{false};

// Written by the render task's drain, read by housekeeping on another core.
// One bit, and nothing is ordered against it, so an atomic is the whole of
// what it needs.
std::atomic<bool> desiredLedState{false};

// How long settings must go unchanged before they are written.
//
// Five seconds is long enough that dragging a colour picker costs one write
// rather than one per frame, and short enough that nobody loses work they
// would notice. What it buys is flash life on the M4, whose settings block is
// erased whole on every write.
//
// The cost is stated rather than hidden: a power cut within five seconds of a
// change loses that change. docs/flash-storage.md says so.
constexpr uint32_t kPersistQuietMs = 5000;
QuietTimer settingsQuiet(kPersistQuietMs);

// Writes actually made, for /api/metrics. The number that shows the debounce
// working: fifty changes in five seconds should be one.
uint32_t persistWrites = 0;
// Events that had to be dropped because a listener was not keeping up.
uint32_t eventsDropped = 0;

// The API layer's whole view of the firmware. Assembled once here; handlers
// reach for nothing else.
// Which apps exist is the variant's decision; it also tells us whether there
// is a MultiViewer poll to report on.
AppRegistry appRegistry{appScheduler, clockSource, nullptr, nullptr};

// Forwards an inbound WebSocket message to the same validation and the same
// command queue the REST route uses. A free function because WsHub takes a
// plain function pointer -- it has no business knowing what an ApiContext is.
void onWsMessage(const char *json, size_t len, void *user) {
  api::handleWsMessage(json, len, *static_cast<ApiContext *>(user));
}

WsTicketStore wsTickets;

ApiContext apiContext{credentials,  authenticator,   pairing,
                      clockSource,  appScheduler,    desiredLedState,
                      FIRMWARE_VERSION, nullptr, nullptr, wsTickets};

WsHub wsHub(onWsMessage, &apiContext);

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
void drainCommands(uint32_t nowMs) {
  Command command{};
  while (rtos::commandTake(command)) {
    switch (command.kind) {
      case Command::Kind::kSwitchApp: {
        appScheduler.switchTo(command.appIndex);
        Serial.print(F("[apps] switched to "));
        Serial.println(appScheduler.activeName());

        Event event{};
        event.kind = Event::Kind::kAppSwitched;
        event.appIndex = command.appIndex;
        if (!rtos::eventPost(event)) ++eventsDropped;
        break;
      }

      case Command::Kind::kApplySetting:
        // Validated by the poster against the same bag, so a refusal here
        // means validate and apply disagree -- a bug worth hearing about
        // rather than a bad request.
        if (appScheduler.applySetting(command.appIndex, command.key, command.value)) {
          settingsQuiet.mark(nowMs);

          Event event{};
          event.kind = Event::Kind::kSettingChanged;
          event.appIndex = command.appIndex;
          strncpy(event.key, command.key, sizeof(event.key) - 1);
          if (!rtos::eventPost(event)) ++eventsDropped;
        } else {
          Serial.print(F("[apps] setting rejected after validation: "));
          Serial.println(command.key);
        }
        break;

      case Command::Kind::kSetLed:
        desiredLedState.store(command.on, std::memory_order_relaxed);
        break;
    }
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
  digitalWrite(LED_BUILTIN, desiredLedState.load(std::memory_order_relaxed) ? HIGH : LOW);
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

// ---------------------------------------------------------------------------
// The four jobs
//
// Same code on both boards. The M4 runs them in turn, so each waits for the
// last and a slow poll stalls the panel -- accepted, and unchanged from what
// it always did. The S3 runs the first three as pinned tasks, so it does not.
// Which happens is board/exec.h's decision; see docs/concurrency.md for who
// owns what once they run at once.
// ---------------------------------------------------------------------------

// Owns the display and every app, and is the only writer of app state.
void renderTick(uint32_t nowMs) {
  // First, so a mutation queued since the last pass is in effect before
  // anything draws with it.
  drainCommands(nowMs);
  appScheduler.update(nowMs);
}

// Turns queued events into WebSocket messages.
//
// The network task is the only consumer, by design: it owns the connections,
// so nothing else can be part-way through writing a frame. Phase 6.2 adds MQTT
// as a second thing to notify, and it is fed from here rather than from its
// own drain -- one consumer means events cannot be delivered to one listener
// and missed by another.
void broadcastEvents() {
  Event event{};
  while (rtos::eventTake(event)) {
    JsonDocument doc;

    switch (event.kind) {
      case Event::Kind::kAppSwitched:
        doc["event"] = "app_switched";
        doc["app"] = event.appIndex;
        doc["name"] = appScheduler.name(event.appIndex);
        break;

      case Event::Kind::kSettingChanged: {
        doc["event"] = "setting_changed";
        doc["app"] = event.appIndex;
        doc["key"] = event.key;

        // Read here rather than carried in the event: by the time this runs
        // the value may have changed again, and the current one is what a
        // listener wants. getSetting takes the settings mutex.
        SettingValue value{};
        if (appScheduler.getSetting(event.appIndex, event.key, value)) {
          switch (value.type) {
            case SettingType::kBool:
              doc["value"] = value.boolValue;
              break;
            case SettingType::kInt:
            case SettingType::kColor:
              doc["value"] = value.intValue;
              break;
            case SettingType::kString:
              doc["value"] = value.stringValue;
              break;
          }
        }
        break;
      }
    }

    char text[192];
    const size_t len = serializeJson(doc, text, sizeof(text));
    if (len > 0) wsHub.broadcast(text, len);
  }
}

// Accepts and serves one request. Blocks as much as it likes: on the S3
// nothing waits for it, and on the M4 this is where the wait always was.
void netTick(uint32_t nowMs) {
  (void)nowMs;

  // Housekeeping saw the button; doing the work here keeps every write to the
  // credential store on one task.
  if (revokeAllRequested.exchange(false, std::memory_order_relaxed)) {
    if (credentials.clear()) {
      authenticator.forgetAll();
      Serial.println(F("[creds] all clients revoked"));
    } else {
      Serial.println(F("[creds] nothing to revoke"));
    }
  }

  if (net_link::maintain()) {
    // The listening socket does not survive a reconnect on every board, and
    // maintain() reports the edge so it can be rebound.
    net_link::serveHttp(80);
  }


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

// Polls MultiViewer, if this build has an app that wants one. The variant
// supplies it, so a build with no F1 app links no transport and this is null.
void mvTick(uint32_t nowMs) {
  if (appRegistry.mvPoll != nullptr) appRegistry.mvPoll(nowMs);
}

// Buttons, pairing, the LED, clock upkeep. Cheap, periodic, and holds nothing
// anyone else is waiting for.
void housekeepTick(uint32_t nowMs) {
  buttonUp.poll();
  buttonDown.poll();

  if (buttonUp.takePress()) {
    pairing.open();
    Serial.print(F("[pair] window open for "));
    Serial.print(PairingWindow::kWindowMs / 1000);
    Serial.println(F("s"));
  }

  // Handed to the network task rather than done here; see revokeAllRequested.
  if (buttonDown.takeHold(kFactoryResetHoldMs)) {
    revokeAllRequested.store(true, std::memory_order_relaxed);
  }

  // Persistence moved off the render task in 5.2. saveAll() reads every app's
  // settings through the mutex, so it is safe here -- and it takes 4.6 KB of
  // stack, which the render task no longer has to carry.
  if (settingsQuiet.due(nowMs)) {
    appSettingsStore.saveAll(appScheduler);
    settingsQuiet.clear();
    ++persistWrites;
  }

  clockSource.maintain();
  updateLed();
  metrics::recordPersistWrites(persistWrites);
  metrics::recordEventsDropped(eventsDropped);
  metrics::tick();
  reportMetrics();
}

// Services open WebSocket connections and delivers events to them.
//
// Its own tick, not part of netTick, because serving one HTTP request means
// blocking on that client's socket -- and a browser holding a socket open
// should not go quiet because someone else is uploading slowly. On the S3 that
// makes it a task of its own; on the M4 it is one more call in the sequence,
// with the same blocking the rest of that board has.
void wsTick(uint32_t nowMs) {
  (void)nowMs;
  broadcastEvents();
  wsHub.poll();
}

constexpr exec::Ticks kTicks{renderTick, netTick, wsTick, mvTick, housekeepTick};

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
  fs::begin();
  credentials.begin();
  buttonUp.begin(board_pins::kButtonUp);
  buttonDown.begin(board_pins::kButtonDown);

  // Bring up the RGB matrix before anything WiFi-related blocks, so the
  // clock app can render while the board is still negotiating a connection.
  registerApps(appRegistry);
  apiContext.mvCounters = appRegistry.mvCounters;
  apiContext.wsHub = &wsHub;
  appSettingsStore.begin(appScheduler);
  const MatrixBeginStatus matrixStatus = appScheduler.begin();
  if (matrixStatus != kMatrixBeginOk) {
    Serial.print(F("Matrix begin() failed, status="));
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

  // Last: a task started here may run before the next statement does, so
  // everything the ticks touch has to be up already.
  exec::start(kTicks);
}


void loop() {
  metrics::markLoop();
  exec::tick(kTicks, millis());
}
