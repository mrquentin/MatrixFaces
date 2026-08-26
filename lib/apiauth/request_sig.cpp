#include "request_sig.h"

#include <cstring>

#include "hex.h"
#include "hmac_sha256.h"

namespace apiauth {
namespace {

// Appends `text` to `out` at `offset`, returning false if it would overflow.
bool append(char *out, size_t cap, size_t &offset, const char *text) {
  const size_t len = strlen(text);
  if (offset + len >= cap) return false;
  memcpy(out + offset, text, len);
  offset += len;
  return true;
}

bool appendChar(char *out, size_t cap, size_t &offset, char c) {
  if (offset + 1 >= cap) return false;
  out[offset++] = c;
  return true;
}

}  // namespace

size_t buildCanonicalRequest(const char *method, const char *path, const char *ts,
                             const char *nonce, const uint8_t bodyHash[Sha256::kDigestSize],
                             char *out, size_t cap) {
  if (cap == 0) return 0;

  char bodyHashHex[Sha256::kDigestSize * 2 + 1];
  toHex(bodyHash, Sha256::kDigestSize, bodyHashHex);

  size_t offset = 0;
  const bool ok = append(out, cap, offset, method) && appendChar(out, cap, offset, '\n') &&
                  append(out, cap, offset, path) && appendChar(out, cap, offset, '\n') &&
                  append(out, cap, offset, ts) && appendChar(out, cap, offset, '\n') &&
                  append(out, cap, offset, nonce) && appendChar(out, cap, offset, '\n') &&
                  append(out, cap, offset, bodyHashHex);
  if (!ok) {
    out[0] = '\0';
    return 0;
  }

  out[offset] = '\0';
  return offset;
}

bool signRequest(const uint8_t *secret, size_t secretLen, const char *method, const char *path,
                 const char *ts, const char *nonce, const void *body, size_t bodyLen,
                 uint8_t out[Sha256::kDigestSize]) {
  uint8_t bodyHash[Sha256::kDigestSize];
  Sha256::hash(body, bodyLen, bodyHash);

  char canonical[kCanonicalRequestMax];
  const size_t len = buildCanonicalRequest(method, path, ts, nonce, bodyHash, canonical,
                                           sizeof(canonical));
  if (len == 0) return false;

  hmacSha256(secret, secretLen, canonical, len, out);
  return true;
}

bool verifyRequestSignature(const uint8_t *secret, size_t secretLen, const char *method,
                            const char *path, const char *ts, const char *nonce, const void *body,
                            size_t bodyLen, const char *sigHex) {
  uint8_t provided[Sha256::kDigestSize];
  if (!fromHex(sigHex, strlen(sigHex), provided, sizeof(provided))) return false;

  uint8_t expected[Sha256::kDigestSize];
  if (!signRequest(secret, secretLen, method, path, ts, nonce, body, bodyLen, expected)) {
    return false;
  }

  return constantTimeEquals(expected, provided, sizeof(expected));
}

}  // namespace apiauth
