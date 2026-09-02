#include "net/mv_link.h"

#include <cstring>

void MvLink::publish(const Snapshot &snapshot) {
  rtos::LockGuard guard(mutex_);
  snapshot_ = snapshot;
}

MvLink::Snapshot MvLink::read() const {
  rtos::LockGuard guard(mutex_);
  return snapshot_;
}

void MvLink::requestHost(const char *host) {
  rtos::LockGuard guard(mutex_);
  if (host == nullptr) {
    host_[0] = '\0';
  } else {
    strncpy(host_, host, sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = '\0';
  }
  hostChanged_ = true;
}

bool MvLink::takeHostChange(char *out, size_t cap) {
  rtos::LockGuard guard(mutex_);
  if (!hostChanged_) return false;
  hostChanged_ = false;

  if (out != nullptr && cap > 0) {
    strncpy(out, host_, cap - 1);
    out[cap - 1] = '\0';
  }
  return true;
}
