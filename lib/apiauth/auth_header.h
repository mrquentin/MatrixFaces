#pragma once

#include <cstddef>
#include <cstdint>

namespace apiauth {

// Sizes of the wire representation of each credential field.
constexpr size_t kClientIdBytes = 8;
constexpr size_t kNonceBytes = 8;
constexpr size_t kSecretBytes = 32;
constexpr size_t kSignatureBytes = 32;

constexpr size_t kClientIdHexLen = kClientIdBytes * 2;
constexpr size_t kNonceHexLen = kNonceBytes * 2;
constexpr size_t kSecretHexLen = kSecretBytes * 2;
constexpr size_t kSignatureHexLen = kSignatureBytes * 2;

// A parsed `Authorization: HMAC id=..,ts=..,nonce=..,sig=..` header.
struct AuthHeader {
  char id[kClientIdHexLen + 1];
  char nonce[kNonceHexLen + 1];
  char sig[kSignatureHexLen + 1];
  // The timestamp is kept both as text and as a number: the canonical request is
  // signed over the exact characters the client sent, so re-formatting the number
  // would break otherwise valid signatures.
  char ts[11];
  uint32_t timestamp;
};

// Parses the value of an Authorization header. Returns false unless the scheme is
// HMAC and all four parameters are present and well formed.
bool parseAuthHeader(const char *value, AuthHeader &out);

}  // namespace apiauth
