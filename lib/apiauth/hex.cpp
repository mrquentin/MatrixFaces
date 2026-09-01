#include "hex.h"

namespace apiauth {
namespace {

constexpr char kDigits[] = "0123456789abcdef";

int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

void toHex(const uint8_t *in, size_t len, char *out) {
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = kDigits[in[i] >> 4];
    out[i * 2 + 1] = kDigits[in[i] & 0x0f];
  }
  out[len * 2] = '\0';
}

bool fromHex(const char *in, size_t inLen, uint8_t *out, size_t outLen) {
  if (inLen != outLen * 2) return false;

  for (size_t i = 0; i < outLen; ++i) {
    const int hi = nibble(in[i * 2]);
    const int lo = nibble(in[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool constantTimeEquals(const void *a, const void *b, size_t len) {
  const auto *pa = static_cast<const uint8_t *>(a);
  const auto *pb = static_cast<const uint8_t *>(b);
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) {
    diff |= static_cast<uint8_t>(pa[i] ^ pb[i]);
  }
  return diff == 0;
}

}  // namespace apiauth
