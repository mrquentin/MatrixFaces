#pragma once

#include <cstddef>
#include <cstdint>

// The on-flash framing for a fixed-capacity array of POD records:
//
//   offset 0                        uint32  magic
//   offset 4                        uint16  version
//   offset 6                        uint16  count
//   offset 8                        Record  records[maxRecords]
//   offset 8 + size*maxRecords      uint32  crc32 of everything before it
//
// This layout is load-bearing. A board taking a firmware update has to read
// back the credentials the previous firmware wrote, or every paired client is
// silently lost -- with no error, just an empty store. It is byte-for-byte what
// FlashRecordStore used to write, it is pinned by goldens in test_blob_store,
// and it must not change without a version bump and a migration path.
//
// Records must be trivially copyable, 4-byte aligned and a multiple of 4 bytes
// long, which is what makes the layout above free of padding. Callers assert
// this for their own record type.
namespace record_blob {

// CRC-32 (reflected, polynomial 0xEDB88320, initial 0xFFFFFFFF, final XOR),
// the same one the flash store has always used.
uint32_t crc32(const void *data, size_t len);

// Framed size for a full-capacity blob. Always the full capacity, never just
// `count` records: the CRC has to cover a deterministic span, and erased flash
// beyond it would otherwise drift into the checksum.
constexpr size_t framedSize(size_t recordSize, uint16_t maxRecords) {
  return 8 + recordSize * maxRecords + 4;
}

// Writes the framed blob to `out`. Records beyond `count` are zero-filled so
// the same logical contents always produce the same bytes. False if `out` is
// too small or `count` exceeds `maxRecords`.
bool serialize(void *out, size_t outCap, uint32_t magic, uint16_t version, const void *records,
               size_t recordSize, uint16_t maxRecords, uint16_t count);

enum class ParseResult : uint8_t {
  kOk,
  kTooShort,     // fewer bytes than the framing needs
  kNoData,       // magic or version does not match: blank or another blob
  kBadChecksum,  // corrupt, or a half-finished write
  kBadCount,     // count larger than the blob can hold
};

// Reads a framed blob into `records`, which must have room for `maxRecords`.
// On anything but kOk, `records` and `outCount` are left untouched, so a bad
// read never destroys what is already in memory.
ParseResult parse(const void *in, size_t inLen, uint32_t magic, uint16_t version, void *records,
                  size_t recordSize, uint16_t maxRecords, uint16_t &outCount);

// Human-readable reason, for the one log line each store prints on startup.
const char *describe(ParseResult result);

}  // namespace record_blob
