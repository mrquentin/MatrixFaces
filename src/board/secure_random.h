#pragma once

#include <cstddef>
#include <cstdint>

// The SAMD51's hardware true random number generator. Arduino's random() is
// seeded identically on every boot, so it must not be used for credentials.
namespace secure_random {

void begin();

// Fills `out` with random bytes. Returns false if the peripheral stopped
// producing data, in which case `out` is zeroed and the caller must fail the
// operation rather than fall back to a weaker source.
bool bytes(uint8_t *out, size_t len);

}  // namespace secure_random
