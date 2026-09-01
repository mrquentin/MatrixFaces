#include "app_settings_store.h"

#include <Arduino.h>

#include <cstring>

namespace {

void copyCapped(char *dest, size_t cap, const char *src) {
  strncpy(dest, src, cap - 1);
  dest[cap - 1] = '\0';
}

}  // namespace

void AppSettingsStore::begin(AppScheduler &scheduler) {
  // If the program ever grows into the storage block, an upload would start
  // overwriting settings again. checkPlacement() logs specifics.
  store_.checkPlacement();
  load(scheduler);
}

void AppSettingsStore::load(AppScheduler &scheduler) {
  SettingRecord records[kMaxRecords];
  uint16_t count = 0;
  if (!store_.load(records, count)) return;

  uint16_t applied = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const SettingRecord &record = records[i];

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

  store_.save(records, count);
}
