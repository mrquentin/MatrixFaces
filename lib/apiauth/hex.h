#pragma once

#include <cstddef>
#include <cstdint>

namespace apiauth {

// Writes 2 * len lowercase hex characters plus a NUL. `out` needs 2 * len + 1 bytes.
void toHex(const uint8_t *in, size_t len, char *out);

// Decodes exactly `outLen` bytes from `2 * outLen` hex characters. Returns false on
// any non-hex character or a length mismatch.
bool fromHex(const char *in, size_t inLen, uint8_t *out, size_t outLen);

// Comparison whose running time does not depend on where the first difference is.
bool constantTimeEquals(const void *a, const void *b, size_t len);

}  // namespace apiauth
