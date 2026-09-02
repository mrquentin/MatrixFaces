#pragma once

#include <cstdint>

#include "board/rtos.h"

// The command queue for boards that run one task.
//
// Header-only, and shared by src/board/samd51 and src/board/native, which
// would otherwise write the same ring buffer out twice -- the same reasoning
// as board/metrics_counters.h. The ESP32-S3 does not use it: there the queue
// has to be safe across cores, so it is a FreeRTOS queue instead.
//
// No locking, deliberately. On these boards there is exactly one task, and
// pretending otherwise would put a no-op mutex around a ring buffer and invite
// someone to believe it was safe against an interrupt. It is not: nothing here
// may be posted from an ISR.
namespace rtos {

class CommandRing {
 public:
  bool post(const Command &command) {
    if (count_ >= kCommandQueueDepth) return false;
    items_[(head_ + count_) % kCommandQueueDepth] = command;
    ++count_;
    return true;
  }

  bool take(Command &out) {
    if (count_ == 0) return false;
    out = items_[head_];
    head_ = (head_ + 1) % kCommandQueueDepth;
    --count_;
    return true;
  }

  uint8_t free() const { return static_cast<uint8_t>(kCommandQueueDepth - count_); }

  // For tests, which need to start from a known state.
  void clear() {
    head_ = 0;
    count_ = 0;
  }

 private:
  Command items_[kCommandQueueDepth];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

}  // namespace rtos
