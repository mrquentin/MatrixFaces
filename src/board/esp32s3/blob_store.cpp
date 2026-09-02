#include "board/blob_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

// Named blobs in NVS. Twenty lines instead of the M4's erase-block driver,
// because the wear levelling, the checksums and the atomic replace are all
// things NVS already does -- and the framing on top is record_blob's, shared
// with the M4 and byte-identical either way, so the same credentials file
// round-trips through both boards.
//
// The contents are stored in the clear. NVS can encrypt, but the key would
// live in the same flash the attacker already has in their hand, and it
// complicates recovery and OTA for a threat model it does not actually change.
// See docs/security.md.
namespace blob_store {
namespace {

// NVS namespaces and keys are capped at 15 characters; "creds" and "settings"
// are the blob names from the contract and fit as they are.
constexpr const char *kNamespace = "matrixfaces";

bool known(const char *name) {
  return name != nullptr &&
         (strcmp(name, kCredentials) == 0 || strcmp(name, kAppSettings) == 0);
}

void logUnknown(const char *name) {
  Serial.print(F("[blob] FATAL: unknown blob name '"));
  Serial.print(name != nullptr ? name : "(null)");
  Serial.println(F("'"));
}

// A slot that has never been written reads back the way erased flash does on
// the M4: all ones. That is what makes record_blob::parse answer "no stored
// data" rather than "shorter than its framing", so both boards take the same
// path on a blank device.
void reportBlank(void *buf, size_t cap, size_t &outLen) {
  memset(buf, 0xFF, cap);
  outLen = cap;
}

}  // namespace

bool checkPlacement(const char *name) {
  if (!known(name)) {
    logUnknown(name);
    return false;
  }

  // Nothing to place: the partition table reserves NVS and the bootloader
  // hands it over intact. Opening the namespace read-write is the one thing
  // that can still fail -- a partition too small, or one that never got
  // formatted -- and it is worth finding out at boot rather than at the first
  // pairing. Creates the namespace on a fresh board, which is harmless.
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.print(F("["));
    Serial.print(name);
    Serial.println(F("] FATAL: NVS namespace would not open"));
    return false;
  }
  prefs.end();
  return true;
}

bool load(const char *name, void *buf, size_t cap, size_t &outLen) {
  if (!known(name)) {
    logUnknown(name);
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    // Read-only open fails when the namespace does not exist yet, which is
    // exactly the state of a board that has never saved anything.
    reportBlank(buf, cap, outLen);
    return true;
  }

  // Zero means either no such key or a stored blob that does not fit `cap`.
  // Both are "nothing usable here", and the framing is what decides whether
  // what we hand back is meaningful.
  const size_t read = prefs.getBytes(name, buf, cap);
  prefs.end();

  if (read == 0) {
    reportBlank(buf, cap, outLen);
    return true;
  }
  outLen = read;
  return true;
}

bool save(const char *name, const void *buf, size_t len) {
  if (!known(name)) {
    logUnknown(name);
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.print(F("["));
    Serial.print(name);
    Serial.println(F("] NVS namespace would not open for writing"));
    return false;
  }

  const size_t written = prefs.putBytes(name, buf, len);
  prefs.end();

  if (written != len) {
    Serial.print(F("["));
    Serial.print(name);
    Serial.println(F("] NVS write was short; data may not persist"));
    return false;
  }
  return true;
}

}  // namespace blob_store
