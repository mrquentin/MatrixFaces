#include "app_scheduler.h"

bool AppScheduler::add(App &app) {
  if (count_ >= kMaxApps) return false;
  apps_[count_++] = &app;
  return true;
}

ProtomatterStatus AppScheduler::begin() {
  const ProtomatterStatus status = matrix_.begin();
  if (status == PROTOMATTER_OK && count_ > 0) {
    apps_[activeIndex_]->begin(matrix_);
  }
  return status;
}

void AppScheduler::switchTo(uint8_t index) {
  if (index >= count_ || index == activeIndex_) return;
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

uint8_t AppScheduler::settingCount(uint8_t appIndex) const {
  return appIndex < count_ ? apps_[appIndex]->settingCount() : 0;
}

const SettingDescriptor &AppScheduler::settingDescriptor(uint8_t appIndex,
                                                          uint8_t settingIndex) const {
  static constexpr SettingDescriptor kNone{"", "", SettingType::kBool, 0, 0, 0};
  if (appIndex >= count_ || settingIndex >= apps_[appIndex]->settingCount()) return kNone;
  return apps_[appIndex]->settingDescriptor(settingIndex);
}

bool AppScheduler::getSetting(uint8_t appIndex, const char *key, SettingValue &out) const {
  return appIndex < count_ && apps_[appIndex]->getSetting(key, out);
}

bool AppScheduler::setSetting(uint8_t appIndex, const char *key, const SettingValue &value) {
  return appIndex < count_ && apps_[appIndex]->setSetting(key, value);
}

void AppScheduler::update(uint32_t nowMs) {
  if (count_ == 0) return;
  apps_[activeIndex_]->update(matrix_, nowMs);
}
