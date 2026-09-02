#pragma once

#include <cstddef>
#include <cstdint>

#include "auth_header.h"

// One paired client. The secret is stored in the clear because HMAC needs the
// original key material to recompute a signature.
struct StoredClient {
  uint8_t id[apiauth::kClientIdBytes];
  uint8_t secret[apiauth::kSecretBytes];
  uint32_t pairedAt;
};

// This record goes to storage byte for byte, and a board taking a firmware
// update has to find its existing pairings intact. Changing the layout means
// bumping kVersion and writing a migration, not just editing the struct.
static_assert(sizeof(StoredClient) == 44, "on-storage credential layout is fixed");
static_assert(alignof(StoredClient) == 4, "record_blob assumes 4-byte alignment");

// Persists paired clients so they survive a reboot. Writes only happen on
// pair, revoke and reset, so flash wear is a non-issue at any realistic
// pairing rate.
class CredentialStore {
 public:
  static constexpr uint8_t kMaxClients = 4;

  void begin();

  uint8_t count() const { return count_; }
  bool full() const { return count_ >= kMaxClients; }

  const StoredClient *find(const uint8_t id[apiauth::kClientIdBytes]) const;
  const StoredClient &at(uint8_t index) const { return clients_[index]; }

  // All three persist immediately and return false only when nothing changed.
  bool add(const StoredClient &client);
  bool remove(const uint8_t id[apiauth::kClientIdBytes]);
  bool clear();

 private:
  static constexpr uint32_t kMagic = 0x4d345753;  // "M4WS"
  static constexpr uint16_t kVersion = 1;

  void load();
  // Serialises the in-memory clients to storage; the object itself is unchanged.
  void save() const;

  StoredClient clients_[kMaxClients];
  uint8_t count_ = 0;
};
