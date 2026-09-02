#pragma once

#include <cstdint>

// Only as much of Arduino's IPAddress as the Client interface needs to compile
// on the host.
class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : value_(static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
               (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24)) {}

  bool operator==(const IPAddress &other) const { return value_ == other.value_; }
  bool operator!=(const IPAddress &other) const { return !(*this == other); }

  operator uint32_t() const { return value_; }

 private:
  uint32_t value_ = 0;
};
