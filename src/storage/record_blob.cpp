#include "storage/record_blob.h"

#include <cstring>

namespace record_blob {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kCountOffset = 6;
constexpr size_t kRecordsOffset = 8;

// Little-endian accessors rather than a struct overlay, so the layout is
// explicit and cannot be shifted by a compiler's padding decisions.
uint32_t readU32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const uint8_t *p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

void writeU32(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}

void writeU16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
}

}  // namespace

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

bool serialize(void *out, size_t outCap, uint32_t magic, uint16_t version, const void *records,
               size_t recordSize, uint16_t maxRecords, uint16_t count) {
  const size_t total = framedSize(recordSize, maxRecords);
  if (outCap < total || count > maxRecords) return false;

  auto *bytes = static_cast<uint8_t *>(out);
  memset(bytes, 0, total);

  writeU32(bytes + kMagicOffset, magic);
  writeU16(bytes + kVersionOffset, version);
  writeU16(bytes + kCountOffset, count);
  if (count > 0) memcpy(bytes + kRecordsOffset, records, recordSize * count);

  const size_t crcOffset = total - 4;
  writeU32(bytes + crcOffset, crc32(bytes, crcOffset));
  return true;
}

ParseResult parse(const void *in, size_t inLen, uint32_t magic, uint16_t version, void *records,
                  size_t recordSize, uint16_t maxRecords, uint16_t &outCount) {
  const size_t total = framedSize(recordSize, maxRecords);
  if (inLen < total) return ParseResult::kTooShort;

  const auto *bytes = static_cast<const uint8_t *>(in);

  // Erased flash reads as 0xFF, so magic and version are what distinguish real
  // data from a blank block or a blob belonging to something else.
  if (readU32(bytes + kMagicOffset) != magic) return ParseResult::kNoData;
  if (readU16(bytes + kVersionOffset) != version) return ParseResult::kNoData;

  const size_t crcOffset = total - 4;
  if (readU32(bytes + crcOffset) != crc32(bytes, crcOffset)) return ParseResult::kBadChecksum;

  const uint16_t count = readU16(bytes + kCountOffset);
  if (count > maxRecords) return ParseResult::kBadCount;

  if (count > 0) memcpy(records, bytes + kRecordsOffset, recordSize * count);
  outCount = count;
  return ParseResult::kOk;
}

const char *describe(ParseResult result) {
  switch (result) {
    case ParseResult::kOk:
      return "ok";
    case ParseResult::kTooShort:
      return "blob shorter than its framing";
    case ParseResult::kNoData:
      return "no stored data";
    case ParseResult::kBadChecksum:
      return "stored data failed checksum, ignoring";
    case ParseResult::kBadCount:
      return "stored count out of range, ignoring";
  }
  return "unknown";
}

}  // namespace record_blob
