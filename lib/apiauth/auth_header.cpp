#include "auth_header.h"

#include <cstring>

#include "hex.h"

namespace apiauth {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool matchesIgnoringCase(const char *a, const char *b, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

bool isHexString(const char *s, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = s[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) return false;
  }
  return true;
}

// Copies the parameter value at `begin`..`end` into `dest` when it is exactly
// `expectedLen` characters long.
bool takeValue(const char *begin, const char *end, size_t expectedLen, char *dest) {
  if (static_cast<size_t>(end - begin) != expectedLen) return false;
  memcpy(dest, begin, expectedLen);
  dest[expectedLen] = '\0';
  return true;
}

}  // namespace

bool parseAuthHeader(const char *value, AuthHeader &out) {
  if (value == nullptr) return false;

  memset(&out, 0, sizeof(out));

  const char *p = value;
  while (isSpace(*p)) ++p;

  constexpr char kScheme[] = "HMAC";
  constexpr size_t schemeLen = sizeof(kScheme) - 1;
  if (strlen(p) < schemeLen || !matchesIgnoringCase(p, kScheme, schemeLen)) return false;
  p += schemeLen;
  if (!isSpace(*p)) return false;

  bool haveId = false, haveTs = false, haveNonce = false, haveSig = false;

  while (*p != '\0') {
    while (isSpace(*p) || *p == ',') ++p;
    if (*p == '\0') break;

    const char *nameBegin = p;
    while (*p != '=' && *p != '\0' && *p != ',') ++p;
    if (*p != '=') return false;
    const auto nameLen = static_cast<size_t>(p - nameBegin);
    ++p;  // skip '='

    const char *valueBegin = p;
    while (*p != ',' && *p != '\0' && !isSpace(*p)) ++p;
    const char *valueEnd = p;

    if (nameLen == 2 && memcmp(nameBegin, "id", 2) == 0) {
      if (!takeValue(valueBegin, valueEnd, kClientIdHexLen, out.id)) return false;
      if (!isHexString(out.id, kClientIdHexLen)) return false;
      haveId = true;
    } else if (nameLen == 2 && memcmp(nameBegin, "ts", 2) == 0) {
      const auto len = static_cast<size_t>(valueEnd - valueBegin);
      if (len == 0 || len >= sizeof(out.ts)) return false;
      uint64_t parsed = 0;
      for (size_t i = 0; i < len; ++i) {
        const char c = valueBegin[i];
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10 + static_cast<uint64_t>(c - '0');
      }
      if (parsed > 0xffffffffu) return false;
      memcpy(out.ts, valueBegin, len);
      out.ts[len] = '\0';
      out.timestamp = static_cast<uint32_t>(parsed);
      haveTs = true;
    } else if (nameLen == 5 && memcmp(nameBegin, "nonce", 5) == 0) {
      if (!takeValue(valueBegin, valueEnd, kNonceHexLen, out.nonce)) return false;
      if (!isHexString(out.nonce, kNonceHexLen)) return false;
      haveNonce = true;
    } else if (nameLen == 3 && memcmp(nameBegin, "sig", 3) == 0) {
      if (!takeValue(valueBegin, valueEnd, kSignatureHexLen, out.sig)) return false;
      if (!isHexString(out.sig, kSignatureHexLen)) return false;
      haveSig = true;
    } else {
      // Unknown parameters are rejected rather than ignored: everything that
      // influences the request must be covered by the signature.
      return false;
    }
  }

  return haveId && haveTs && haveNonce && haveSig;
}

}  // namespace apiauth
