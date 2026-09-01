#pragma once

#include <cstdint>

// The 60 second window, opened by the UP button, during which POST /pair is
// accepted. It closes on the first successful pairing.
class PairingWindow {
 public:
  static constexpr uint32_t kWindowMs = 60000;

  void open();
  void close();

  bool isOpen() const;
  uint32_t remainingSeconds() const;

 private:
  bool armed_ = false;
  uint32_t deadlineMs_ = 0;
};
