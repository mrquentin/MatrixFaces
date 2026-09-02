#include "board/secure_random.h"

#include <esp_random.h>

// The ESP32-S3's hardware RNG. Arduino's random() is seeded identically on
// every boot, so it must not be used for credentials.
namespace secure_random {

void begin() {
  // Nothing to enable: esp_fill_random() is available from reset. It is only
  // *true* random once the RF subsystem is running, before which it degrades
  // to a pseudo-random source seeded at startup. The only caller is pairing,
  // which cannot happen before the link is up, so this build never reads it in
  // the degraded window -- a constraint worth restating if that ever changes.
}

bool bytes(uint8_t *out, size_t len) {
  esp_fill_random(out, len);
  // No failure mode to report: unlike the SAMD51's TRNG there is no data-ready
  // flag to time out on, and the call cannot come back short.
  return true;
}

}  // namespace secure_random
