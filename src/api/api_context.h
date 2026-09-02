#pragma once

#include "api/authenticator.h"
#include "api/credential_store.h"
#include "api/pairing_window.h"
#include "apps/app_scheduler.h"
#include "apps/app_settings_store.h"
#include "board/time_source.h"
#include "net/multiviewer_parse.h"

// Everything the API layer is allowed to touch, handed in by the composition
// root rather than reached for as file-scope globals. Two reasons: the
// handlers become callable without standing up the whole firmware, and the
// API's dependency surface is auditable in one glance instead of scattered
// across 900 lines.
struct ApiContext {
  CredentialStore &credentials;
  Authenticator &authenticator;
  PairingWindow &pairing;
  TimeSource &clock;
  AppScheduler &scheduler;
  AppSettingsStore &settingsStore;

  // Owned by main.cpp: the LED is board feedback, driven from loop().
  bool &desiredLedState;

  const char *firmwareVersion;

  // Signal strength is a radio detail, and reading it directly would drag
  // WiFiNINA back into the API layer that phase 1.1 just got it out of. The
  // composition root supplies it; board/net_link.h absorbs this in phase 2.
  int32_t (*rssiDbm)();

  // Read-only view of the MultiViewer poll's health, for /api/metrics. The
  // pure parse header rather than the client, so the API layer picks up no
  // transport dependency.
  const mv::Counters *mvCounters;
};
