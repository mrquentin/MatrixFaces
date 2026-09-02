#pragma once

#include "api/authenticator.h"
#include "api/credential_store.h"
#include "api/pairing_window.h"
#include "apps/app_scheduler.h"
#include "board/time_source.h"
#include "net/multiviewer_parse.h"

// Everything the API layer is allowed to touch, handed in by the composition
// root rather than reached for as file-scope globals.
//
// Note what is *not* here: persistence. Handlers used to call saveAll()
// themselves; since phase 4.1 they post a command and the render loop saves
// after applying it, so the API layer has no reason to know storage exists. Two reasons: the
// handlers become callable without standing up the whole firmware, and the
// API's dependency surface is auditable in one glance instead of scattered
// across 900 lines.
struct ApiContext {
  CredentialStore &credentials;
  Authenticator &authenticator;
  PairingWindow &pairing;
  TimeSource &clock;
  AppScheduler &scheduler;

  // Owned by main.cpp: the LED is board feedback, driven from loop().
  bool &desiredLedState;

  const char *firmwareVersion;

  // Read-only view of the MultiViewer poll's health, for /api/metrics. The
  // pure parse header rather than the client, so the API layer picks up no
  // transport dependency.
  const mv::Counters *mvCounters;
};
