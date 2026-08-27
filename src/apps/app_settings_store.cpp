#include "app_settings_store.h"

#include <Arduino.h>

#include <cstddef>
#include <cstring>

#include "board/flash_block.h"

extern "C" {
// End of the image the uploader writes. Used to prove the storage block sits
// beyond it. The name is the linker's, so it cannot be renamed to satisfy the
// reserved-identifier check. Redeclared here rather than shared with
// CredentialStore's identical declaration: an extern is per-translation-unit,
// and pulling in a cross-module header for one symbol isn't worth it.
extern char __etext;  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
}

namespace {

constexpr uint32_t kMagic = 0x4d344153;  // "M4AS"
constexpr uint16_t kVersion = 1;
constexpr uint8_t kMaxRecords = 32;
constexpr size_t kNameCap = 16;

struct SettingRecord {
  char appName[kNameCap];
  char key[kNameCap];
  SettingValue value;
};

struct StoredBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  SettingRecord records[kMaxRecords];
  uint32_t crc;
};

static_assert(sizeof(StoredBlob) <= flash_block::kBlockSize, "blob exceeds one erase block");
static_assert(sizeof(StoredBlob) % 4 == 0, "flash writes happen in 32-bit words");

// Identical to CredentialStore's; duplicated rather than shared because
// pulling in a cross-module dependency for one CRC routine isn't worth it.
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

// Everything up to but excluding the trailing crc field.
size_t checksummedLength() { return offsetof(StoredBlob, crc); }

void copyCapped(char *dest, size_t cap, const char *src) {
  strncpy(dest, src, cap - 1);
  dest[cap - 1] = '\0';
}

}  // namespace

void AppSettingsStore::begin(AppScheduler &scheduler) {
  // Same overlap check CredentialStore performs for its own block: cheap,
  // and silent corruption from a grown image is otherwise very hard to
  // attribute.
  const auto imageEnd = reinterpret_cast<uint32_t>(&__etext);
  if (imageEnd >= flash_block::kAppSettingsAddress) {
    Serial.print(F("[settings] FATAL: image ends at 0x"));
    Serial.print(imageEnd, HEX);
    Serial.print(F(" which overlaps the storage block at 0x"));
    Serial.println(flash_block::kAppSettingsAddress, HEX);
  }
  if (!flash_block::geometryMatches()) {
    Serial.println(F("[settings] FATAL: unexpected flash page geometry"));
  }

  load(scheduler);
}

void AppSettingsStore::load(AppScheduler &scheduler) {
  StoredBlob blob{};
  flash_block::read(flash_block::kAppSettingsAddress, &blob, sizeof(blob));

  // Erased flash reads as 0xFF, so the magic and CRC are what distinguish
  // real data from a blank or half-written block.
  if (blob.magic != kMagic || blob.version != kVersion) {
    Serial.println(F("[settings] no stored app settings"));
    return;
  }
  if (blob.crc != crc32(&blob, checksummedLength())) {
    Serial.println(F("[settings] stored app settings failed checksum, ignoring"));
    return;
  }
  if (blob.count > kMaxRecords) {
    Serial.println(F("[settings] stored app settings count out of range, ignoring"));
    return;
  }

  uint16_t applied = 0;
  for (uint16_t i = 0; i < blob.count; ++i) {
    const SettingRecord &record = blob.records[i];

    for (uint8_t appIndex = 0; appIndex < scheduler.count(); ++appIndex) {
      if (strcmp(scheduler.name(appIndex), record.appName) != 0) continue;
      if (scheduler.setSetting(appIndex, record.key, record.value)) ++applied;
      break;
    }
  }

  Serial.print(F("[settings] restored "));
  Serial.print(applied);
  Serial.println(F(" app setting(s)"));
}

void AppSettingsStore::saveAll(const AppScheduler &scheduler) {
  StoredBlob blob{};
  blob.magic = kMagic;
  blob.version = kVersion;

  uint16_t count = 0;
  for (uint8_t appIndex = 0; appIndex < scheduler.count() && count < kMaxRecords; ++appIndex) {
    const uint8_t settingCount = scheduler.settingCount(appIndex);
    for (uint8_t s = 0; s < settingCount && count < kMaxRecords; ++s) {
      const SettingDescriptor &descriptor = scheduler.settingDescriptor(appIndex, s);

      SettingRecord &record = blob.records[count];
      if (!scheduler.getSetting(appIndex, descriptor.key, record.value)) continue;

      copyCapped(record.appName, sizeof(record.appName), scheduler.name(appIndex));
      copyCapped(record.key, sizeof(record.key), descriptor.key);
      ++count;
    }
  }
  blob.count = count;
  blob.crc = crc32(&blob, checksummedLength());

  if (!flash_block::erasedWrite(flash_block::kAppSettingsAddress, &blob, sizeof(blob))) {
    Serial.println(F("[settings] flash write did not verify; settings may not persist"));
  }
}
