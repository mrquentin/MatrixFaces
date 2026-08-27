#pragma once

#include "app_scheduler.h"

// Persists every registered app's settings across a reboot, in a dedicated
// erase block separate from CredentialStore's.
//
// Records are keyed by (app name, setting key) rather than app index, so a
// firmware update that reorders or inserts registered apps can't silently
// misapply a stored value to the wrong app -- a stored record simply stops
// matching anything if its app or key no longer exists.
class AppSettingsStore {
 public:
  // Validates the flash geometry/placement, then restores every stored
  // record that still matches a currently registered app and setting. Call
  // once at startup, after every app has been registered with `scheduler`.
  void begin(AppScheduler &scheduler);

  // Snapshots every registered app's every current setting value and writes
  // it to flash. Call after any setting change; writes are infrequent and
  // user-driven, so flash wear is a non-issue at any realistic rate.
  void saveAll(const AppScheduler &scheduler);

 private:
  void load(AppScheduler &scheduler);
};
