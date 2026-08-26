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

void AppScheduler::update(uint32_t nowMs) {
  if (count_ == 0) return;
  apps_[activeIndex_]->update(matrix_, nowMs);
}
