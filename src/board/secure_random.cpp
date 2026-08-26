#include "secure_random.h"

#include <Arduino.h>

#include <cstring>

namespace secure_random {
namespace {

// The TRNG produces a fresh word roughly every 84 clock cycles; this bound is
// several orders of magnitude beyond that, so it only trips on real failure.
constexpr uint32_t kWordTimeoutMicros = 10000;

bool nextWord(uint32_t &out) {
  const uint32_t start = micros();
  while (!TRNG->INTFLAG.bit.DATARDY) {
    if (micros() - start > kWordTimeoutMicros) return false;
  }
  out = TRNG->DATA.reg;  // Reading DATA clears DATARDY.
  return true;
}

}  // namespace

void begin() {
  MCLK->APBCMASK.bit.TRNG_ = 1;
  TRNG->CTRLA.bit.ENABLE = 1;
}

bool bytes(uint8_t *out, size_t len) {
  size_t offset = 0;
  while (offset < len) {
    uint32_t word;
    if (!nextWord(word)) {
      memset(out, 0, len);
      return false;
    }

    const size_t remaining = len - offset;
    const size_t take = remaining < sizeof(word) ? remaining : sizeof(word);
    memcpy(out + offset, &word, take);
    offset += take;
  }
  return true;
}

}  // namespace secure_random
