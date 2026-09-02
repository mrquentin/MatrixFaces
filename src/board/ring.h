#pragma once

#include <cstdint>

// The queues, for boards that run one task.
//
// Header-only, and shared by src/board/samd51 and src/board/native, which
// would otherwise write the same ring buffer out twice -- the same reasoning
// as board/metrics_counters.h. The ESP32-S3 does not use it: there the queues
// have to be safe across cores, so they are FreeRTOS queues instead.
//
// No locking, deliberately. On these boards there is exactly one task, and
// pretending otherwise would put a no-op mutex around a ring buffer and invite
// someone to believe it was safe against an interrupt. It is not: nothing here
// may be posted from an ISR.
namespace rtos {

// `dropOldest` is what separates the two users. A command must not be lost --
// a refused one becomes a 503 and the client retries -- so its ring refuses
// when full. An event is a notification, and a listener that has fallen behind
// wants the current state rather than the start of the backlog, so its ring
// makes room instead.
template <typename T, uint8_t Depth, bool dropOldest>
class Ring {
 public:
  bool post(const T &item) {
    bool dropped = false;
    if (count_ >= Depth) {
      if (!dropOldest) return false;
      head_ = (head_ + 1) % Depth;
      --count_;
      dropped = true;
    }
    items_[(head_ + count_) % Depth] = item;
    ++count_;
    return !dropped;
  }

  bool take(T &out) {
    if (count_ == 0) return false;
    out = items_[head_];
    head_ = (head_ + 1) % Depth;
    --count_;
    return true;
  }

  uint8_t free() const { return static_cast<uint8_t>(Depth - count_); }

  // For tests, which need to start from a known state.
  void clear() {
    head_ = 0;
    count_ = 0;
  }

 private:
  T items_[Depth];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

}  // namespace rtos
