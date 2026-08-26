#pragma once

#include <cstddef>
#include <cstdint>

#include "auth_header.h"
#include "credential_store.h"
#include "time_source.h"

enum AuthResult {
  kAuthOk,
  kAuthMissingHeader,
  kAuthMalformedHeader,
  kAuthUnknownClient,
  kAuthClockUnavailable,
  kAuthStaleTimestamp,
  kAuthReplay,
  kAuthBadSignature,
};

const char *authResultCode(AuthResult result);

// Verifies HMAC-signed requests and rejects replays.
class Authenticator {
 public:
  // Requests are accepted within this many seconds either side of the device's
  // clock, which bounds how long a captured request stays usable.
  static constexpr uint32_t kMaxSkewSeconds = 60;

  Authenticator(CredentialStore &store, TimeSource &time) : store_(store), time_(time) {}

  // On success writes the caller's client id to `outClientId`.
  AuthResult authenticate(const char *authorization, const char *method, const char *path,
                          const void *body, size_t bodyLen,
                          uint8_t outClientId[apiauth::kClientIdBytes]);

  // Drops per-client replay state, e.g. after a client is revoked.
  void forget(const uint8_t id[apiauth::kClientIdBytes]);
  void forgetAll();

 private:
  // Sized well above the request rate a single board sustains inside one skew
  // window; older entries are simply overwritten.
  static constexpr uint8_t kNonceCacheSize = 24;

  struct NonceEntry {
    uint8_t clientId[apiauth::kClientIdBytes];
    uint8_t nonce[apiauth::kNonceBytes];
    uint32_t timestamp;
    bool used;
  };

  struct HighWaterMark {
    uint8_t clientId[apiauth::kClientIdBytes];
    uint32_t timestamp;
    bool used;
  };

  bool nonceSeen(const uint8_t clientId[apiauth::kClientIdBytes],
                 const uint8_t nonce[apiauth::kNonceBytes]) const;
  void rememberNonce(const uint8_t clientId[apiauth::kClientIdBytes],
                     const uint8_t nonce[apiauth::kNonceBytes], uint32_t timestamp);
  HighWaterMark *highWaterMark(const uint8_t clientId[apiauth::kClientIdBytes]);

  CredentialStore &store_;
  TimeSource &time_;

  NonceEntry nonces_[kNonceCacheSize] = {};
  uint8_t nonceNext_ = 0;
  HighWaterMark marks_[CredentialStore::kMaxClients] = {};
};
