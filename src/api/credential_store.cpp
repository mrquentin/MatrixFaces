#include "credential_store.h"

#include <Arduino.h>

#include <cstring>

#include "board/blob_store.h"
#include "hex.h"
#include "storage/record_blob.h"

namespace {

constexpr size_t kBlobSize =
    record_blob::framedSize(sizeof(StoredClient), CredentialStore::kMaxClients);

}  // namespace

void CredentialStore::begin() {
  // If the program ever grows into the storage block, an upload would start
  // overwriting credentials again. checkPlacement() logs specifics.
  blob_store::checkPlacement(blob_store::kCredentials);
  load();
}

void CredentialStore::load() {
  count_ = 0;
  memset(clients_, 0, sizeof(clients_));

  uint8_t blob[kBlobSize];
  size_t length = 0;
  if (!blob_store::load(blob_store::kCredentials, blob, sizeof(blob), length)) return;

  uint16_t count = 0;
  const record_blob::ParseResult result = record_blob::parse(
      blob, length, kMagic, kVersion, clients_, sizeof(StoredClient), kMaxClients, count);

  if (result != record_blob::ParseResult::kOk) {
    Serial.print(F("[creds] "));
    Serial.println(record_blob::describe(result));
    return;
  }

  count_ = static_cast<uint8_t>(count);
  Serial.print(F("[creds] loaded "));
  Serial.print(count_);
  Serial.println(F(" client(s)"));
}

void CredentialStore::save() const {
  uint8_t blob[kBlobSize];
  if (!record_blob::serialize(blob, sizeof(blob), kMagic, kVersion, clients_,
                              sizeof(StoredClient), kMaxClients, count_)) {
    Serial.println(F("[creds] failed to serialise; not saving"));
    return;
  }
  blob_store::save(blob_store::kCredentials, blob, sizeof(blob));
}

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
