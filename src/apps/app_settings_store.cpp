#include "app_settings_store.h"

#include <Arduino.h>

#include <cstring>

#include "board/blob_store.h"
#include "storage/record_blob.h"

namespace {

void copyCapped(char *dest, size_t cap, const char *src) {
  strncpy(dest, src, cap - 1);
  dest[cap - 1] = '\0';
}

}  // namespace

void AppSettingsStore::begin(AppScheduler &scheduler) {
  // If the program ever grows into the storage block, an upload would start
  // overwriting settings again. checkPlacement() logs specifics.
  blob_store::checkPlacement(blob_store::kAppSettings);
  load(scheduler);
}

void AppSettingsStore::load(AppScheduler &scheduler) {
  SettingRecord records[kMaxRecords];
  constexpr size_t kBlobSize =
      record_blob::framedSize(sizeof(SettingRecord), kMaxRecords);
  uint8_t blob[kBlobSize];
  size_t length = 0;
  if (!blob_store::load(blob_store::kAppSettings, blob, sizeof(blob), length)) return;

  uint16_t count = 0;
  const record_blob::ParseResult result = record_blob::parse(
      blob, length, kMagic, kVersion, records, sizeof(SettingRecord), kMaxRecords, count);
  if (result != record_blob::ParseResult::kOk) {
    Serial.print(F("[settings] "));
    Serial.println(record_blob::describe(result));
    return;
  }

  uint16_t applied = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const SettingRecord &record = records[i];

    for (uint8_t appIndex = 0; appIndex < scheduler.count(); ++appIndex) {
      if (strcmp(scheduler.name(appIndex), record.appName) != 0) continue;
      // Stored records go through the same validation as a live request, so a
      // value that a firmware update has since narrowed out of range is
      // dropped rather than restored.
      if (scheduler.applySetting(appIndex, record.key, record.value)) ++applied;
      break;
    }
  }

  Serial.print(F("[settings] restored "));
  Serial.print(applied);
  Serial.println(F(" app setting(s)"));
}

void AppSettingsStore::saveAll(const AppScheduler &scheduler) {
  SettingRecord records[kMaxRecords];
  uint16_t count = 0;

  for (uint8_t appIndex = 0; appIndex < scheduler.count() && count < kMaxRecords; ++appIndex) {
    const uint8_t settingCount = scheduler.settingCount(appIndex);
    for (uint8_t s = 0; s < settingCount && count < kMaxRecords; ++s) {
      const SettingDescriptor &descriptor = scheduler.settingDescriptor(appIndex, s);

      SettingRecord &record = records[count];
      if (!scheduler.getSetting(appIndex, descriptor.key, record.value)) continue;

      copyCapped(record.appName, sizeof(record.appName), scheduler.name(appIndex));
      copyCapped(record.key, sizeof(record.key), descriptor.key);
      ++count;
    }
  }

  constexpr size_t kBlobSize =
      record_blob::framedSize(sizeof(SettingRecord), kMaxRecords);
  uint8_t blob[kBlobSize];
  if (!record_blob::serialize(blob, sizeof(blob), kMagic, kVersion, records,
                              sizeof(SettingRecord), kMaxRecords, count)) {
    Serial.println(F("[settings] failed to serialise; not saving"));
    return;
  }
  blob_store::save(blob_store::kAppSettings, blob, sizeof(blob));
}
