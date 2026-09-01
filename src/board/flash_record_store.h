#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "board/flash_block.h"

extern "C" {
// End of the image the uploader writes. Used to prove a store's block sits
// beyond it. The name is the linker's, so it cannot be renamed to satisfy the
// reserved-identifier check.
extern char __etext;  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
}

namespace flash_store_detail {
// Shared by every FlashRecordStore instantiation so the routine exists once
// in the binary rather than once per (Record, kMaxRecords) pair.
uint32_t crc32(const void *data, size_t len);
}  // namespace flash_store_detail

// A fixed-capacity array of POD records, persisted to one flash_block erase
// block as magic/version/count/records[kMaxRecords]/crc, with a checksum and
// read-back-verified write. Every concern that needs to survive a reset --
// paired-client credentials, per-app settings, and so on -- gets its own
// instance at its own reserved flash_block address, so writing one concern's
// store never cycles another's erase count.
//
// `Record` must be trivially copyable and small enough that kMaxRecords of
// them, plus the 12-byte header and 4-byte trailer, fit in one erase block --
// enforced below by a static_assert. This class only knows how to move bytes
// to and from flash; matching stored records to live state (by app name, by
// client id, ...) is the owning class's job.
template <typename Record, uint16_t kMaxRecords>
class FlashRecordStore {
 public:
  // `logTag` must outlive this store (a string literal is expected) and
  // appears in every log line, e.g. "[<logTag>] no stored data".
  FlashRecordStore(uint32_t address, uint32_t magic, uint16_t version, const char *logTag)
      : address_(address), magic_(magic), version_(version), logTag_(logTag) {}

  // Validates the flash geometry and that the image doesn't overlap this
  // store's block, logging a FATAL line for either failure. Call once at
  // startup, before load()/save(); a false return means flash access below
  // is not trustworthy on this board.
  bool checkPlacement() const {
    bool ok = true;
    const auto imageEnd = reinterpret_cast<uint32_t>(&__etext);
    if (imageEnd >= address_) {
      Serial.print(F("["));
      Serial.print(logTag_);
      Serial.print(F("] FATAL: image ends at 0x"));
      Serial.print(imageEnd, HEX);
      Serial.print(F(" which overlaps the storage block at 0x"));
      Serial.println(address_, HEX);
      ok = false;
    }
    if (!flash_block::geometryMatches()) {
      Serial.print(F("["));
      Serial.print(logTag_);
      Serial.println(F("] FATAL: unexpected flash page geometry"));
      ok = false;
    }
    return ok;
  }

  // Reads and validates the stored blob. On success, fills `records[0..N)`
  // (caller-owned, capacity kMaxRecords) and `count`, and returns true.
  // Leaves both untouched -- logging why -- if the block is blank, corrupt,
  // or from an incompatible version.
  bool load(Record *records, uint16_t &count) const {
    Blob blob{};
    flash_block::read(address_, &blob, sizeof(blob));

    // Erased flash reads as 0xFF, so the magic and CRC are what distinguish
    // real data from a blank or half-written block.
    if (blob.magic != magic_ || blob.version != version_) {
      Serial.print(F("["));
      Serial.print(logTag_);
      Serial.println(F("] no stored data"));
      return false;
    }
    if (blob.crc != flash_store_detail::crc32(&blob, checksummedLength())) {
      Serial.print(F("["));
      Serial.print(logTag_);
      Serial.println(F("] stored data failed checksum, ignoring"));
      return false;
    }
    if (blob.count > kMaxRecords) {
      Serial.print(F("["));
      Serial.print(logTag_);
      Serial.println(F("] stored count out of range, ignoring"));
      return false;
    }

    memcpy(records, blob.records, blob.count * sizeof(Record));
    count = blob.count;
    return true;
  }

  // Serialises records[0..count) to flash. Returns false -- logging why --
  // if the write did not read back identical.
  bool save(const Record *records, uint16_t count) const {
    Blob blob{};
    blob.magic = magic_;
    blob.version = version_;
    blob.count = count;
    memcpy(blob.records, records, count * sizeof(Record));
    blob.crc = flash_store_detail::crc32(&blob, checksummedLength());

    if (!flash_block::erasedWrite(address_, &blob, sizeof(blob))) {
      Serial.print(F("["));
      Serial.print(logTag_);
      Serial.println(F("] flash write did not verify; data may not persist"));
      return false;
    }
    return true;
  }

 private:
  struct Blob {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    Record records[kMaxRecords];
    uint32_t crc;
  };
  static_assert(sizeof(Blob) <= flash_block::kBlockSize, "blob exceeds one erase block");
  static_assert(sizeof(Blob) % 4 == 0, "flash writes happen in 32-bit words");

  // Everything up to but excluding the trailing crc field.
  static size_t checksummedLength() { return offsetof(Blob, crc); }

  uint32_t address_;
  uint32_t magic_;
  uint16_t version_;
  const char *logTag_;
};
