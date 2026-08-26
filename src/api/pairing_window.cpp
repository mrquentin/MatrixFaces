#include "pairing_window.h"

#include <Arduino.h>

namespace {

// Signed difference so the comparison keeps working across the ~49 day millis()
// rollover, which a plain `millis() < deadline` would get wrong.
bool reached(uint32_t deadlineMs) { return (int32_t)(millis() - deadlineMs) >= 0; }

}  // namespace

void PairingWindow::open() {
  armed_ = true;
  deadlineMs_ = millis() + kWindowMs;
}

void PairingWindow::close() { armed_ = false; }

bool PairingWindow::isOpen() const { return armed_ && !reached(deadlineMs_); }

uint32_t PairingWindow::remainingSeconds() const {
  if (!isOpen()) return 0;
  return (deadlineMs_ - millis() + 999) / 1000;
}
