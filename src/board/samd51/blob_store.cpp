#include "board/blob_store.h"

#include <Arduino.h>

#include <cstring>

#include "board/samd51/flash_block.h"

extern "C" {
// End of the image the uploader writes. Used to prove a blob's block sits
// beyond it. The name is the linker's, so it cannot be renamed to satisfy the
// reserved-identifier check.
extern char __etext;  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
}

namespace blob_store {
namespace {

// Each blob gets its own erase block, so writing one never cycles another's
// erase count. The addresses are the ones the previous firmware used and must
// not move: a board taking an update has to find its credentials where it left
// them.
struct Slot {
  const char *name;
  uint32_t address;
};

constexpr Slot kSlots[] = {
    {kCredentials, flash_block::kCredentialsAddress},
    {kAppSettings, flash_block::kAppSettingsAddress},
};

const Slot *slotFor(const char *name) {
  if (name == nullptr) return nullptr;
  for (const Slot &slot : kSlots) {
    if (strcmp(slot.name, name) == 0) return &slot;
  }
  return nullptr;
}

void logUnknown(const char *name) {
  Serial.print(F("[blob] FATAL: unknown blob name '"));
  Serial.print(name != nullptr ? name : "(null)");
  Serial.println(F("'"));
}

}  // namespace

bool checkPlacement(const char *name) {
  const Slot *slot = slotFor(name);
  if (slot == nullptr) {
    logUnknown(name);
    return false;
  }

  bool ok = true;
  const auto imageEnd = reinterpret_cast<uint32_t>(&__etext);
  if (imageEnd >= slot->address) {
    Serial.print(F("["));
    Serial.print(slot->name);
    Serial.print(F("] FATAL: image ends at 0x"));
    Serial.print(imageEnd, HEX);
    Serial.print(F(" which overlaps the storage block at 0x"));
    Serial.println(slot->address, HEX);
    ok = false;
  }
  if (!flash_block::geometryMatches()) {
    Serial.print(F("["));
    Serial.print(slot->name);
    Serial.println(F("] FATAL: unexpected flash page geometry"));
    ok = false;
  }
  return ok;
}

bool load(const char *name, void *buf, size_t cap, size_t &outLen) {
  const Slot *slot = slotFor(name);
  if (slot == nullptr) {
    logUnknown(name);
    return false;
  }
  if (cap > flash_block::kBlockSize) return false;

  // Raw flash has no stored length: the caller asked for `cap` bytes and that
  // is exactly the span the framing covers.
  flash_block::read(slot->address, buf, cap);
  outLen = cap;
  return true;
}

bool save(const char *name, const void *buf, size_t len) {
  const Slot *slot = slotFor(name);
  if (slot == nullptr) {
    logUnknown(name);
    return false;
  }
  if (len > flash_block::kBlockSize) return false;

  if (!flash_block::erasedWrite(slot->address, buf, len)) {
    Serial.print(F("["));
    Serial.print(slot->name);
    Serial.println(F("] flash write did not verify; data may not persist"));
    return false;
  }
  return true;
}

}  // namespace blob_store
