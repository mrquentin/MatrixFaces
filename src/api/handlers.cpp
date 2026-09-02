#include "api/handlers.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>

#include "api/json_util.h"
#include "api/router.h"
// Unqualified: -Isrc/board/<target> decides whose capabilities these are.
#include "board_caps.h"

#include "board/metrics.h"
#include "board/rtos.h"
#include "board/net_link.h"
#include "board/secure_random.h"
#include "hex.h"

namespace api {
namespace {

// ---------------------------------------------------------------------------
// Shared bits
// ---------------------------------------------------------------------------

constexpr uint16_t kNoAppIndex = 0xFFFF;

// Parses a `/api/apps/<n>/settings` wildcard. Digits only, and bounded to what
// a uint8_t app index can hold; anything else is not an app index at all and
// the caller reports the path as unknown rather than the app as unknown.
//
// Deliberately stricter than the strtoul() this replaces, which also accepted a
// leading '+' -- so GET /api/apps/+1/settings used to return app 1. It now 404s
// like any other unrecognised path.
uint16_t parseAppIndex(const char *text) {
  if (text == nullptr || *text == '\0') return kNoAppIndex;

  uint32_t value = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return kNoAppIndex;
    value = value * 10 + static_cast<uint32_t>(*p - '0');
    if (value > 255) return kNoAppIndex;
  }
  return static_cast<uint16_t>(value);
}

const char *settingTypeName(SettingType type) {
  switch (type) {
    case SettingType::kBool:
      return "bool";
    case SettingType::kInt:
      return "int";
    case SettingType::kString:
      return "string";
    case SettingType::kColor:
      return "color";
  }
  return "unknown";
}

// `index` is what the reply reports, which is not always what is active yet:
// a switch is queued and lands on the next drain, so the honest answer to
// "which app did you accept" is the one that was asked for. Callers that mean
// "which is running" pass scheduler.activeIndex().
void sendActiveApp(Client &client, ApiContext &ctx, uint8_t index) {
  JsonDocument doc;
  doc["index"] = index;
  doc["name"] = ctx.scheduler.name(index);
  sendJson(client, 200, "OK", doc);
}

// A value a request was accepted with but which has not been applied yet.
struct AcceptedSetting {
  const char *key;
  SettingValue value;
};

// Values only, keyed by descriptor -- the schema itself lives in
// handleListApps. An entry getSetting() refuses is simply omitted.
//
// `accepted` is what a POST just validated and queued. Those are reported in
// preference to what the app currently holds, because the two now differ for
// up to one loop: applies go through the command queue and land on the next
// drain. Reporting the live value would tell the client its request had not
// taken effect, which is both alarming and wrong.
void sendAppSettings(Client &client, ApiContext &ctx, uint8_t appIndex,
                     const AcceptedSetting *accepted = nullptr, uint8_t acceptedCount = 0) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();

  const uint8_t settingCount = ctx.scheduler.settingCount(appIndex);
  for (uint8_t i = 0; i < settingCount; ++i) {
    const SettingDescriptor &descriptor = ctx.scheduler.settingDescriptor(appIndex, i);
    SettingValue value{};

    const AcceptedSetting *pending = nullptr;
    for (uint8_t a = 0; a < acceptedCount; ++a) {
      if (strcmp(accepted[a].key, descriptor.key) == 0) {
        pending = &accepted[a];
        break;
      }
    }

    if (pending != nullptr) {
      value = pending->value;
    } else if (!ctx.scheduler.getSetting(appIndex, descriptor.key, value)) {
      continue;
    }

    switch (descriptor.type) {
      case SettingType::kBool:
        root[descriptor.key] = value.boolValue;
        break;
      case SettingType::kInt:
      case SettingType::kColor:
        root[descriptor.key] = value.intValue;
        break;
      case SettingType::kString:
        root[descriptor.key] = value.stringValue;
        break;
    }
  }

  sendJson(client, 200, "OK", doc);
}

// ---------------------------------------------------------------------------
// Unauthenticated handlers
// ---------------------------------------------------------------------------

// Discovery endpoint. Exposes only what a client needs before it has
// credentials.
void handleRoot(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  JsonDocument doc;
  doc["device"] = "matrixfaces";
  doc["firmware_version"] = ctx.firmwareVersion;
  doc["paired_clients"] = ctx.credentials.count();
  doc["pairing_open"] = ctx.pairing.isOpen();
  doc["pairing_expires_in"] = ctx.pairing.remainingSeconds();
  doc["clock_synced"] = ctx.clock.isValid();
  sendJson(client, 200, "OK", doc);
}

void handlePair(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  if (!ctx.pairing.isOpen()) {
    // Deliberately the same response whether the window never opened or has
    // already expired.
    sendErrorResponse(client, 401, "Unauthorized", "pairing_closed");
    return;
  }
  if (ctx.credentials.full()) {
    sendErrorResponse(client, 409, "Conflict", "too_many_clients");
    return;
  }

  StoredClient stored{};

  // A collision across 8 random bytes is vanishingly unlikely, but retrying is
  // cheaper than reasoning about what a duplicate id would do to lookups.
  bool generated = false;
  for (int attempt = 0; attempt < 4 && !generated; ++attempt) {
    if (!secure_random::bytes(stored.id, sizeof(stored.id))) break;
    if (ctx.credentials.find(stored.id) == nullptr) generated = true;
  }
  if (!generated || !secure_random::bytes(stored.secret, sizeof(stored.secret))) {
    Serial.println(F("[pair] TRNG unavailable, refusing to issue credentials"));
    sendErrorResponse(client, 500, "Internal Server Error", "rng_unavailable");
    return;
  }

  stored.pairedAt = ctx.clock.isValid() ? ctx.clock.now() : 0;

  if (!ctx.credentials.add(stored)) {
    sendErrorResponse(client, 409, "Conflict", "too_many_clients");
    return;
  }

  // One successful pairing closes the window, as does the 60 second timeout.
  ctx.pairing.close();

  char idHex[apiauth::kClientIdHexLen + 1];
  char secretHex[apiauth::kSecretHexLen + 1];
  apiauth::toHex(stored.id, sizeof(stored.id), idHex);
  apiauth::toHex(stored.secret, sizeof(stored.secret), secretHex);

  // The one response still built by hand rather than through ArduinoJson:
  // this is the only time the secret is ever transmitted, and a fixed buffer
  // is something we can scrub afterwards. A JsonDocument's pool is not
  // reachable for zeroing, so the secret would linger in freed memory.
  char json[192];
  snprintf(json, sizeof(json), R"({"client_id":"%s","secret":"%s","algorithm":"HMAC-SHA256"})",
           idHex, secretHex);
  sendJsonResponse(client, 200, "OK", json);

  // The secret is intentionally never logged; this is its only appearance.
  Serial.print(F("[pair] issued credentials to client "));
  Serial.println(idHex);

  memset(json, 0, sizeof(json));
  memset(secretHex, 0, sizeof(secretHex));
  memset(&stored, 0, sizeof(stored));
}

// ---------------------------------------------------------------------------
// Authenticated handlers
// ---------------------------------------------------------------------------

void handleStatus(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  JsonDocument doc;
  // Which board answered. Two of them run this firmware and a client that has
  // just paired over the network has no other way to tell them apart.
  doc["board"] = board_caps::kBoardName;
  doc["uptime_s"] = millis() / 1000;
  doc["led"] = ctx.desiredLedState;
  doc["rssi"] = net_link::rssiDbm();
  doc["paired_clients"] = ctx.credentials.count();
  doc["time"] = ctx.clock.isValid() ? ctx.clock.now() : 0;
  sendJson(client, 200, "OK", doc);
}

void handleSetLed(Client &client, const HttpRequest &request, const char *, ApiContext &ctx) {
  JsonDocument body;
  if (!parseJsonBody(request, body) || !body["on"].is<bool>()) {
    sendErrorResponse(client, 400, "Bad Request", "expected_on_boolean");
    return;
  }

  Command command{};
  command.kind = Command::Kind::kSetLed;
  command.on = body["on"].as<bool>();
  if (!rtos::commandPost(command)) {
    sendErrorResponse(client, 503, "Service Unavailable", "busy");
    return;
  }

  JsonDocument doc;
  doc["led"] = command.on;
  sendJson(client, 200, "OK", doc);
}

void handleListClients(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  JsonDocument doc;
  JsonArray clients = doc["clients"].to<JsonArray>();

  for (uint8_t i = 0; i < ctx.credentials.count(); ++i) {
    const StoredClient &stored = ctx.credentials.at(i);
    char idHex[apiauth::kClientIdHexLen + 1];
    apiauth::toHex(stored.id, sizeof(stored.id), idHex);

    JsonObject entry = clients.add<JsonObject>();
    entry["id"] = idHex;
    entry["paired_at"] = stored.pairedAt;
  }

  sendJson(client, 200, "OK", doc);
}

void handleListApps(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  JsonDocument doc;
  JsonArray apps = doc["apps"].to<JsonArray>();

  for (uint8_t i = 0; i < ctx.scheduler.count(); ++i) {
    JsonObject app = apps.add<JsonObject>();
    app["index"] = i;
    app["name"] = ctx.scheduler.name(i);
    JsonArray settings = app["settings"].to<JsonArray>();

    const uint8_t settingCount = ctx.scheduler.settingCount(i);
    for (uint8_t s = 0; s < settingCount; ++s) {
      const SettingDescriptor &descriptor = ctx.scheduler.settingDescriptor(i, s);
      JsonObject entry = settings.add<JsonObject>();
      entry["key"] = descriptor.key;
      entry["label"] = descriptor.label;
      entry["type"] = settingTypeName(descriptor.type);

      switch (descriptor.type) {
        case SettingType::kInt:
        case SettingType::kColor:
          entry["min"] = descriptor.intMin;
          entry["max"] = descriptor.intMax;
          break;
        case SettingType::kString:
          entry["max_len"] = descriptor.maxLen;
          break;
        case SettingType::kBool:
          break;
      }
    }
  }

  doc["active_index"] = ctx.scheduler.activeIndex();
  doc["active_name"] = ctx.scheduler.activeName();
  sendJson(client, 200, "OK", doc);
}

void handleGetActiveApp(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  sendActiveApp(client, ctx, ctx.scheduler.activeIndex());
}

void handleSetActiveApp(Client &client, const HttpRequest &request, const char *, ApiContext &ctx) {
  JsonDocument body;
  if (!parseJsonBody(request, body) || !body["index"].is<uint32_t>()) {
    sendErrorResponse(client, 400, "Bad Request", "expected_index_integer");
    return;
  }

  const uint32_t index = body["index"].as<uint32_t>();
  if (index >= ctx.scheduler.count()) {
    sendErrorResponse(client, 400, "Bad Request", "unknown_app_index");
    return;
  }

  Command command{};
  command.kind = Command::Kind::kSwitchApp;
  command.appIndex = static_cast<uint8_t>(index);
  if (!rtos::commandPost(command)) {
    sendErrorResponse(client, 503, "Service Unavailable", "busy");
    return;
  }

  sendActiveApp(client, ctx, command.appIndex);
}

// Resolves the wildcard to a registered app, or writes the error response and
// returns false. The two failures are distinct on purpose: a wildcard that is
// not a number never named an endpoint, while a numeric one that is out of
// range named an app that does not exist.
bool resolveAppIndex(Client &client, const char *wildcard, ApiContext &ctx, uint8_t &out) {
  const uint16_t parsed = parseAppIndex(wildcard);
  if (parsed == kNoAppIndex) {
    sendErrorResponse(client, 404, "Not Found", "unknown_endpoint");
    return false;
  }
  if (parsed >= ctx.scheduler.count()) {
    sendErrorResponse(client, 404, "Not Found", "unknown_app_index");
    return false;
  }
  out = static_cast<uint8_t>(parsed);
  return true;
}

void handleGetAppSettings(Client &client, const HttpRequest &, const char *wildcard,
                          ApiContext &ctx) {
  uint8_t appIndex = 0;
  if (!resolveAppIndex(client, wildcard, ctx, appIndex)) return;
  sendAppSettings(client, ctx, appIndex);
}

// Converts the JSON value for `descriptor.key` into a SettingValue. This is
// only the JSON-shape half; range and length are the SettingsBag's to judge,
// so the limits live in exactly one place.
bool readSettingValue(JsonVariantConst raw, const SettingDescriptor &descriptor,
                      SettingValue &value) {
  value.type = descriptor.type;

  switch (descriptor.type) {
    case SettingType::kBool:
      if (!raw.is<bool>()) return false;
      value.boolValue = raw.as<bool>();
      return true;

    case SettingType::kInt:
    case SettingType::kColor:
      if (!raw.is<int32_t>()) return false;
      value.intValue = raw.as<int32_t>();
      return true;

    case SettingType::kString: {
      if (!raw.is<const char *>()) return false;
      const char *text = raw.as<const char *>();
      const size_t len = strlen(text);
      // A string too long for SettingValue itself cannot be carried far enough
      // to be judged, so it is rejected here rather than silently clipped.
      if (len >= sizeof(value.stringValue)) return false;
      memcpy(value.stringValue, text, len + 1);
      return true;
    }
  }
  return false;
}

// Applies every setting the body mentions, against the target app's own
// descriptors -- no per-app knowledge here, only the generic key/type contract
// every App implements. A key the body omits is left untouched (partial
// update); a key present but malformed or out of range rejects the whole
// request, and rejects it *before* touching any app state, so a later invalid
// field cannot leave an earlier one applied.
void handleSetAppSettings(Client &client, const HttpRequest &request, const char *wildcard,
                          ApiContext &ctx) {
  uint8_t appIndex = 0;
  if (!resolveAppIndex(client, wildcard, ctx, appIndex)) return;

  JsonDocument body;
  if (!parseJsonBody(request, body)) {
    sendErrorResponse(client, 400, "Bad Request", "invalid_setting_value");
    return;
  }

  const uint8_t settingCount = ctx.scheduler.settingCount(appIndex);

  // Pass 1: validate everything present.
  bool anyPresent = false;
  for (uint8_t i = 0; i < settingCount; ++i) {
    const SettingDescriptor &descriptor = ctx.scheduler.settingDescriptor(appIndex, i);
    JsonVariantConst raw = body[descriptor.key];
    if (raw.isNull()) continue;  // absent, or explicitly null: leave it alone
    anyPresent = true;

    SettingValue value{};
    if (!readSettingValue(raw, descriptor, value) ||
        !ctx.scheduler.validateSetting(appIndex, descriptor.key, value)) {
      sendErrorResponse(client, 400, "Bad Request", "invalid_setting_value");
      return;
    }
  }

  if (!anyPresent) {
    sendErrorResponse(client, 400, "Bad Request", "no_recognized_settings");
    return;
  }

  // Pass 2: queue. Everything here was validated above, and the room for all
  // of it is checked before any of it is posted -- half an update applied and
  // a 503 returned would be worse than refusing the lot.
  // Bounded by the queue rather than by a constant of its own: one command per
  // setting, so a request naming more than the queue can hold could never be
  // served whole however large this array was.
  AcceptedSetting accepted[rtos::kCommandQueueDepth];
  uint8_t acceptedCount = 0;
  bool tooManyToQueue = false;

  for (uint8_t i = 0; i < settingCount; ++i) {
    const SettingDescriptor &descriptor = ctx.scheduler.settingDescriptor(appIndex, i);
    JsonVariantConst raw = body[descriptor.key];
    if (raw.isNull()) continue;

    if (acceptedCount >= rtos::kCommandQueueDepth) {
      tooManyToQueue = true;
      break;
    }
    accepted[acceptedCount].key = descriptor.key;
    readSettingValue(raw, descriptor, accepted[acceptedCount].value);
    ++acceptedCount;
  }

  if (tooManyToQueue || rtos::commandFree() < acceptedCount) {
    sendErrorResponse(client, 503, "Service Unavailable", "busy");
    return;
  }

  for (uint8_t i = 0; i < acceptedCount; ++i) {
    Command command{};
    command.kind = Command::Kind::kApplySetting;
    command.appIndex = appIndex;
    strncpy(command.key, accepted[i].key, sizeof(command.key) - 1);
    command.value = accepted[i].value;
    // Space was reserved above, so this cannot fail.
    rtos::commandPost(command);
  }

  // Persistence is no longer this handler's job: saveAll() here would write
  // the values these commands are about to replace. main.cpp saves after the
  // drain instead. Phase 5.2 makes that debounced rather than immediate.
  sendAppSettings(client, ctx, appIndex, accepted, acceptedCount);
}

void handleRevokeClient(Client &client, const HttpRequest &, const char *wildcard,
                        ApiContext &ctx) {
  uint8_t id[apiauth::kClientIdBytes];
  if (strlen(wildcard) != apiauth::kClientIdHexLen ||
      !apiauth::fromHex(wildcard, apiauth::kClientIdHexLen, id, sizeof(id))) {
    sendErrorResponse(client, 400, "Bad Request", "invalid_client_id");
    return;
  }

  if (!ctx.credentials.remove(id)) {
    sendErrorResponse(client, 404, "Not Found", "unknown_client");
    return;
  }

  ctx.authenticator.forget(id);
  Serial.print(F("[creds] revoked client "));
  Serial.println(wildcard);

  JsonDocument doc;
  doc["revoked"] = true;
  sendJson(client, 200, "OK", doc);
}

#if METRICS_ENABLED
void handleMetrics(Client &client, const HttpRequest &, const char *, ApiContext &ctx) {
  const metrics::Snapshot m = metrics::snapshot();

  JsonDocument doc;
  JsonObject cpu = doc["cpu"].to<JsonObject>();
  cpu["loop_hz"] = m.loopHz;
  cpu["busy_permille"] = m.busyPermille;
  cpu["requests"] = m.requests;
  cpu["req_avg_us"] = m.requestAvgMicros;
  cpu["req_max_us"] = m.requestMaxMicros;
  cpu["auth_avg_us"] = m.authAvgMicros;
  cpu["auth_max_us"] = m.authMaxMicros;

  JsonObject ram = doc["ram"].to<JsonObject>();
  ram["total"] = m.ramTotal;
  ram["static"] = m.ramStatic;
  ram["heap_used"] = m.heapUsed;
  ram["stack_peak"] = m.stackPeak;
  ram["free_now"] = m.freeNow;
  ram["min_free_ever"] = m.minFreeEver;

  // Omitted rather than reported as zero on a board with no external RAM, so
  // "no psram section" and "psram exhausted" cannot be confused.
  if (m.psramTotal != 0) {
    JsonObject psram = doc["psram"].to<JsonObject>();
    psram["total"] = m.psramTotal;
    psram["free"] = m.psramFree;
  }

  // Poll health. `parsed` climbing while the display sits on stale data is the
  // signal that the feed's shape changed under us; `malformed` or `truncated`
  // climbing says the same thing more loudly.
  if (ctx.mvCounters != nullptr) {
    const mv::Counters &c = *ctx.mvCounters;
    JsonObject multiViewer = doc["multiviewer"].to<JsonObject>();
    multiViewer["polls"] = c.polls;
    multiViewer["connects"] = c.connects;
    multiViewer["connect_failures"] = c.connectFailures;
    multiViewer["empty_responses"] = c.emptyResponses;
    multiViewer["framing_errors"] = c.framingErrors;
    multiViewer["parsed"] = c.parsed;
    multiViewer["no_session"] = c.noSession;
    multiViewer["malformed"] = c.malformed;
    multiViewer["truncated"] = c.truncated;
  }

  sendJson(client, 200, "OK", doc);
}
#endif  // METRICS_ENABLED

// ---------------------------------------------------------------------------
// Route table
// ---------------------------------------------------------------------------

constexpr Route kRoutes[] = {
    {"GET", "/", handleRoot, false},
    {"POST", "/pair", handlePair, false},

    {"GET", "/api/status", handleStatus, true},
    {"POST", "/api/led", handleSetLed, true},
    {"GET", "/api/clients", handleListClients, true},
    {"GET", "/api/apps", handleListApps, true},
    {"GET", "/api/app", handleGetActiveApp, true},
    {"POST", "/api/app", handleSetActiveApp, true},
    {"GET", "/api/apps/*/settings", handleGetAppSettings, true},
    {"POST", "/api/apps/*/settings", handleSetAppSettings, true},
#if METRICS_ENABLED
    {"GET", "/api/metrics", handleMetrics, true},
#endif
    {"DELETE", "/api/clients/*", handleRevokeClient, true},
};

constexpr size_t kRouteCount = sizeof(kRoutes) / sizeof(kRoutes[0]);

}  // namespace

void handleRequest(Client &client, const HttpRequest &request, ApiContext &ctx) {
  char wildcard[kWildcardCap];
  const Route *route =
      findRoute(kRoutes, kRouteCount, request.method, request.path, wildcard, sizeof(wildcard));

  if (route != nullptr && !route->requiresAuth) {
    route->handler(client, request, wildcard, ctx);
    return;
  }

  // Everything under /api requires a valid signature -- including paths that
  // do not exist, so the API surface cannot be enumerated without credentials.
  // Note the signature covers request.target, query string included, not the
  // routing path.
  if (strncmp(request.path, "/api/", 5) != 0) {
    sendErrorResponse(client, 404, "Not Found", "unknown_endpoint");
    return;
  }

  uint8_t clientId[apiauth::kClientIdBytes];
  const uint32_t authStartCycles = metrics::cycles();
  const AuthResult result =
      ctx.authenticator.authenticate(request.authorization, request.method, request.target,
                                     request.body, request.bodyLen, clientId);
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

  if (route == nullptr) {
    sendErrorResponse(client, 404, "Not Found", "unknown_endpoint");
    return;
  }

  route->handler(client, request, wildcard, ctx);
}

}  // namespace api
