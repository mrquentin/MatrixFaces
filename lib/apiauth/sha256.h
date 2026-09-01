#pragma once

#include <cstddef>
#include <cstdint>

namespace apiauth {

// Streaming SHA-256. Deliberately free of Arduino headers so the known-answer
// tests can run on the host.
class Sha256 {
 public:
  static constexpr size_t kDigestSize = 32;
  static constexpr size_t kBlockSize = 64;

  Sha256() { reset(); }

  void reset();
  void update(const void *data, size_t len);
  void finish(uint8_t out[kDigestSize]);

  static void hash(const void *data, size_t len, uint8_t out[kDigestSize]);

 private:
  void compress(const uint8_t block[kBlockSize]);

  uint32_t state_[8];
  uint64_t bitLength_;
  uint8_t buffer_[kBlockSize];
  size_t bufferLen_;
};

}  // namespace apiauth
