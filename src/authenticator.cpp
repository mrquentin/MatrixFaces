#include "authenticator.h"

#include <cstring>

#include "hex.h"
#include "request_sig.h"

const char *authResultCode(AuthResult result) {
  switch (result) {
    case kAuthOk:
      return "ok";
    case kAuthMissingHeader:
      return "missing_authorization";
    case kAuthMalformedHeader:
      return "malformed_authorization";
    case kAuthUnknownClient:
      return "unknown_client";
    case kAuthClockUnavailable:
      return "clock_unavailable";
    case kAuthStaleTimestamp:
      return "stale_timestamp";
    case kAuthReplay:
      return "replay_detected";
    case kAuthBadSignature:
      return "bad_signature";
  }
  return "unknown";
}

bool Authenticator::nonceSeen(const uint8_t clientId[apiauth::kClientIdBytes],
                              const uint8_t nonce[apiauth::kNonceBytes]) const {
  for (const NonceEntry &entry : nonces_) {
    if (!entry.used) continue;
    if (memcmp(entry.clientId, clientId, apiauth::kClientIdBytes) != 0) continue;
    if (memcmp(entry.nonce, nonce, apiauth::kNonceBytes) != 0) continue;
    return true;
  }
  return false;
}

void Authenticator::rememberNonce(const uint8_t clientId[apiauth::kClientIdBytes],
                                  const uint8_t nonce[apiauth::kNonceBytes], uint32_t timestamp) {
  NonceEntry &entry = nonces_[nonceNext_];
  nonceNext_ = static_cast<uint8_t>((nonceNext_ + 1) % kNonceCacheSize);

  memcpy(entry.clientId, clientId, apiauth::kClientIdBytes);
  memcpy(entry.nonce, nonce, apiauth::kNonceBytes);
  entry.timestamp = timestamp;
  entry.used = true;
}

Authenticator::HighWaterMark *Authenticator::highWaterMark(
    const uint8_t clientId[apiauth::kClientIdBytes]) {
  for (HighWaterMark &mark : marks_) {
    if (mark.used && memcmp(mark.clientId, clientId, apiauth::kClientIdBytes) == 0) {
      return &mark;
    }
  }
  for (HighWaterMark &mark : marks_) {
    if (!mark.used) return &mark;
  }
  return nullptr;
}

AuthResult Authenticator::authenticate(const char *authorization, const char *method,
                                       const char *path, const void *body, size_t bodyLen,
                                       uint8_t outClientId[apiauth::kClientIdBytes]) {
  if (authorization == nullptr || authorization[0] == '\0') return kAuthMissingHeader;

  apiauth::AuthHeader header{};
  if (!apiauth::parseAuthHeader(authorization, header)) return kAuthMalformedHeader;

  uint8_t clientId[apiauth::kClientIdBytes];
  uint8_t nonce[apiauth::kNonceBytes];
  if (!apiauth::fromHex(header.id, apiauth::kClientIdHexLen, clientId, sizeof(clientId)) ||
      !apiauth::fromHex(header.nonce, apiauth::kNonceHexLen, nonce, sizeof(nonce))) {
    return kAuthMalformedHeader;
  }

  // Without a trusted clock the skew check is meaningless, and accepting
  // requests anyway would leave replays unbounded in time.
  if (!time_.isValid()) return kAuthClockUnavailable;

  const StoredClient *client = store_.find(clientId);
  if (client == nullptr) return kAuthUnknownClient;

  const uint32_t now = time_.now();
  const uint32_t skew = now > header.timestamp ? now - header.timestamp : header.timestamp - now;
  if (skew > kMaxSkewSeconds) return kAuthStaleTimestamp;

  // The signature is checked before any replay state is touched, so an
  // unauthenticated caller cannot advance a client's high-water mark and lock
  // the real client out.
  if (!apiauth::verifyRequestSignature(client->secret, sizeof(client->secret), method, path,
                                       header.ts, header.nonce, body, bodyLen, header.sig)) {
    return kAuthBadSignature;
  }

  HighWaterMark *mark = highWaterMark(clientId);
  if (mark != nullptr && mark->used && header.timestamp < mark->timestamp) {
    // Older than a request we have already served: a capture being replayed
    // after the nonce cache rolled over.
    return kAuthReplay;
  }
  if (nonceSeen(clientId, nonce)) return kAuthReplay;

  rememberNonce(clientId, nonce, header.timestamp);
  if (mark != nullptr) {
    memcpy(mark->clientId, clientId, apiauth::kClientIdBytes);
    if (!mark->used || header.timestamp > mark->timestamp) {
      mark->timestamp = header.timestamp;
    }
    mark->used = true;
  }

  memcpy(outClientId, clientId, apiauth::kClientIdBytes);
  return kAuthOk;
}

void Authenticator::forget(const uint8_t id[apiauth::kClientIdBytes]) {
  for (NonceEntry &entry : nonces_) {
    if (entry.used && memcmp(entry.clientId, id, apiauth::kClientIdBytes) == 0) {
      entry.used = false;
    }
  }
  for (HighWaterMark &mark : marks_) {
    if (mark.used && memcmp(mark.clientId, id, apiauth::kClientIdBytes) == 0) {
      mark.used = false;
    }
  }
}

void Authenticator::forgetAll() {
  memset(nonces_, 0, sizeof(nonces_));
  memset(marks_, 0, sizeof(marks_));
  nonceNext_ = 0;
}
