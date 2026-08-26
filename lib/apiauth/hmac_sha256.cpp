#include "hmac_sha256.h"

#include <cstring>

namespace apiauth {

void hmacSha256(const void *key, size_t keyLen, const void *message, size_t messageLen,
                uint8_t out[Sha256::kDigestSize]) {
  uint8_t block[Sha256::kBlockSize];
  memset(block, 0, sizeof(block));

  if (keyLen > Sha256::kBlockSize) {
    Sha256::hash(key, keyLen, block);
  } else {
    memcpy(block, key, keyLen);
  }

  uint8_t pad[Sha256::kBlockSize];
  for (size_t i = 0; i < Sha256::kBlockSize; ++i) {
    pad[i] = block[i] ^ 0x36;
  }

  uint8_t inner[Sha256::kDigestSize];
  Sha256 ctx;
  ctx.update(pad, sizeof(pad));
  ctx.update(message, messageLen);
  ctx.finish(inner);

  for (size_t i = 0; i < Sha256::kBlockSize; ++i) {
    pad[i] = block[i] ^ 0x5c;
  }

  ctx.reset();
  ctx.update(pad, sizeof(pad));
  ctx.update(inner, sizeof(inner));
  ctx.finish(out);

  // Do not leave the derived key material on the stack.
  memset(block, 0, sizeof(block));
  memset(pad, 0, sizeof(pad));
}

}  // namespace apiauth
