#pragma once

#include <atomic>
#include <cstdint>

// "Something changed; write it once things go quiet."
//
// Settings used to be persisted inside the request that changed them, so a
// slider dragged across a colour picker meant a flash write per frame. On the
// M4 that is an 8 KB erase of a block rated for a finite number of them; on
// the S3 it is NVS churn. Neither is a good answer to a user moving a slider.
//
// The whole state is one atomic word, for the same reason PairingWindow's is:
// it is marked by the task that applies a change and read by the task that
// does the writing, and those are different cores. A flag plus a timestamp can
// be read half-updated, and a stale timestamp means writing *during* the burst
// the debounce exists to collapse -- which is exactly what happened when this
// was two plain fields, measured as three writes where there should have been
// one.
//
// Zero means clean; anything else is the instant the write becomes due. Takes
// its clock as an argument, so the behaviour -- including across the 49-day
// millis() rollover -- is decided in test/test_quiet_timer rather than by
// waiting for it.
class QuietTimer {
 public:
  explicit QuietTimer(uint32_t quietMs) : quietMs_(quietMs) {}

  // Something changed. Restarts the quiet period: a burst writes once, after
  // the last change, rather than once per change.
  void mark(uint32_t nowMs) {
    uint32_t dueAt = nowMs + quietMs_;
    // Zero is the clean marker, so the one millisecond every 49 days that
    // would land on it borrows the next.
    if (dueAt == 0) dueAt = 1;
    dueAtMs_.store(dueAt, std::memory_order_relaxed);
  }

  // True when there is something to write and the quiet period has passed.
  // Does not clear -- the caller decides whether the write happened.
  bool due(uint32_t nowMs) const {
    const uint32_t dueAt = dueAtMs_.load(std::memory_order_relaxed);
    // Signed difference, so the comparison stays right across the rollover
    // that a plain `nowMs >= dueAt` would get wrong.
    return dueAt != 0 && static_cast<int32_t>(nowMs - dueAt) >= 0;
  }

  // There is a change waiting, due or not. What a shutdown path would ask
  // before deciding to flush early.
  bool pending() const { return dueAtMs_.load(std::memory_order_relaxed) != 0; }

  void clear() { dueAtMs_.store(0, std::memory_order_relaxed); }

 private:
  uint32_t quietMs_;
  std::atomic<uint32_t> dueAtMs_{0};
};
