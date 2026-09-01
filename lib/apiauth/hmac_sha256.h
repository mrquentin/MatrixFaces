#pragma once

#include <cstddef>
#include <cstdint>

#include "sha256.h"

namespace apiauth {

// HMAC-SHA256 as defined by RFC 2104.
void hmacSha256(const void *key, size_t keyLen, const void *message, size_t messageLen,
                uint8_t out[Sha256::kDigestSize]);

}  // namespace apiauth
