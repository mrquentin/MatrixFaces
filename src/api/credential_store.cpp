#include "credential_store.h"

#include <Arduino.h>

#include <cstring>

#include "hex.h"

void CredentialStore::begin() {
  // If the program ever grows into the storage block, an upload would start
  // overwriting credentials again. checkPlacement() logs specifics.
  store_.checkPlacement();
  load();
}

void CredentialStore::load() {
  count_ = 0;
  memset(clients_, 0, sizeof(clients_));

  uint16_t count = 0;
  if (!store_.load(clients_, count)) return;
  count_ = static_cast<uint8_t>(count);

  Serial.print(F("[creds] loaded "));
  Serial.print(count_);
  Serial.println(F(" client(s)"));
}

void CredentialStore::save() const { store_.save(clients_, count_); }

const StoredClient *CredentialStore::find(const uint8_t id[apiauth::kClientIdBytes]) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (apiauth::constantTimeEquals(clients_[i].id, id, apiauth::kClientIdBytes)) {
      return &clients_[i];
    }
  }
  return nullptr;
}

bool CredentialStore::add(const StoredClient &client) {
  if (full()) return false;

  clients_[count_] = client;
  ++count_;
  save();
  return true;
}

bool CredentialStore::remove(const uint8_t id[apiauth::kClientIdBytes]) {
  for (uint8_t i = 0; i < count_; ++i) {
    if (!apiauth::constantTimeEquals(clients_[i].id, id, apiauth::kClientIdBytes)) continue;

    for (uint8_t j = i; j + 1 < count_; ++j) {
      clients_[j] = clients_[j + 1];
    }
    --count_;
    memset(&clients_[count_], 0, sizeof(clients_[count_]));
    save();
    return true;
  }
  return false;
}

bool CredentialStore::clear() {
  if (count_ == 0) return false;

  count_ = 0;
  memset(clients_, 0, sizeof(clients_));
  save();
  return true;
}
