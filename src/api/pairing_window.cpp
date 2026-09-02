#include "pairing_window.h"

#include <Arduino.h>

namespace {

// Signed difference so the comparison keeps working across the ~49 day millis()
// rollover, which a plain `millis() < deadline` would get wrong.
bool reached(uint32_t deadlineMs) { return (int32_t)(millis() - deadlineMs) >= 0; }

constexpr uint32_t kClosed = 0;

}  // namespace

void PairingWindow::open() {
  uint32_t deadline = millis() + kWindowMs;
  // Zero is the closed marker, so the one millisecond every 49 days that would
  // land on it borrows the next one instead.
  if (deadline == kClosed) deadline = 1;
  deadlineMs_.store(deadline, std::memory_order_relaxed);
}

void PairingWindow::close() { deadlineMs_.store(kClosed, std::memory_order_relaxed); }

bool PairingWindow::isOpen() const {
  const uint32_t deadline = deadlineMs_.load(std::memory_order_relaxed);
  return deadline != kClosed && !reached(deadline);
}

uint32_t PairingWindow::remainingSeconds() const {
  const uint32_t deadline = deadlineMs_.load(std::memory_order_relaxed);
  if (deadline == kClosed || reached(deadline)) return 0;
  return (deadline - millis() + 999) / 1000;
}
