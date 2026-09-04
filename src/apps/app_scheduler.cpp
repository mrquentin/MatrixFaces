#include "app_scheduler.h"

#include "apps/settings_bag.h"

bool AppScheduler::add(App &app) {
  if (count_ >= kMaxApps) return false;
  apps_[count_++] = &app;
  return true;
}

MatrixBeginStatus AppScheduler::begin() {
  const MatrixBeginStatus status = matrix_.begin();
  if (status == kMatrixBeginOk && count_ > 0) {
    apps_[activeIndex_]->begin(matrix_);
  }
  return status;
}

void AppScheduler::switchTo(uint8_t index) {
  if (index >= count_ || index == activeIndex_) return;
  apps_[activeIndex_]->end();
  activeIndex_ = index;
  matrix_.fillScreen(0);
  matrix_.show();
  apps_[activeIndex_]->begin(matrix_);
}

const char *AppScheduler::activeName() const {
  return count_ > 0 ? apps_[activeIndex_]->name() : "none";
}

const char *AppScheduler::name(uint8_t index) const {
  return index < count_ ? apps_[index]->name() : "";
}

const SettingsBag *AppScheduler::bagFor(uint8_t appIndex) const {
  return appIndex < count_ ? apps_[appIndex]->settings() : nullptr;
}

SettingsBag *AppScheduler::bagFor(uint8_t appIndex) {
  return appIndex < count_ ? apps_[appIndex]->settings() : nullptr;
}

uint8_t AppScheduler::settingCount(uint8_t appIndex) const {
  const SettingsBag *bag = bagFor(appIndex);
  return bag != nullptr ? bag->count() : 0;
}

const SettingDescriptor &AppScheduler::settingDescriptor(uint8_t appIndex,
                                                         uint8_t settingIndex) const {
  const SettingsBag *bag = bagFor(appIndex);
  if (bag == nullptr) return SettingsBag::emptyDescriptor();
  return bag->descriptor(settingIndex);
}

bool AppScheduler::getSetting(uint8_t appIndex, const char *key, SettingValue &out) const {
  const SettingsBag *bag = bagFor(appIndex);
  return bag != nullptr && bag->get(key, out);
}

bool AppScheduler::validateSetting(uint8_t appIndex, const char *key,
                                   const SettingValue &value) const {
  const SettingsBag *bag = bagFor(appIndex);
  return bag != nullptr && bag->validate(key, value);
}

bool AppScheduler::applySetting(uint8_t appIndex, const char *key, const SettingValue &value) {
  SettingsBag *bag = bagFor(appIndex);
  return bag != nullptr && bag->apply(key, value);
}

void AppScheduler::update(uint32_t nowMs) {
  if (count_ == 0) return;
  apps_[activeIndex_]->update(matrix_, nowMs);
}
