#include "flash_record_store.h"

namespace flash_store_detail {

uint32_t crc32(const void *data, size_t len) {
  const auto *p = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; ++bit) {
      // Branchless: turns the low bit into an all-ones or all-zeros mask.
      const uint32_t mask = ~((crc & 1U) - 1U);
      crc = (crc >> 1) ^ (0xedb88320U & mask);  // NOLINT(hicpp-signed-bitwise)
    }
  }
  return ~crc;
}

}  // namespace flash_store_detail
