#pragma once

#include <cstddef>
#include <cstdint>

#include "auth_header.h"
#include "sha256.h"

namespace apiauth {

// Upper bound for a canonical request: method + path + timestamp + nonce + body
// digest, separated by newlines.
constexpr size_t kCanonicalRequestMax = 320;

// Serialises "METHOD\nPATH\nTS\nNONCE\nHEX(SHA256(body))" into `out`.
// Returns the number of characters written, or 0 if the result would not fit.
size_t buildCanonicalRequest(const char *method, const char *path, const char *ts,
                             const char *nonce, const uint8_t bodyHash[Sha256::kDigestSize],
                             char *out, size_t cap);

// Computes the expected signature for a request. Returns false if the canonical
// request does not fit in the fixed buffer.
bool signRequest(const uint8_t *secret, size_t secretLen, const char *method, const char *path,
                 const char *ts, const char *nonce, const void *body, size_t bodyLen,
                 uint8_t out[Sha256::kDigestSize]);

// Recomputes the signature and compares it against `sigHex` in constant time.
bool verifyRequestSignature(const uint8_t *secret, size_t secretLen, const char *method,
                            const char *path, const char *ts, const char *nonce, const void *body,
                            size_t bodyLen, const char *sigHex);

}  // namespace apiauth
