#pragma once

#include <algorithm>
#include <cstdint>

#include "board/metrics.h"

#if METRICS_ENABLED

namespace metrics {

// The board-independent half of the instrumentation: loop rate, busy share,
// and the request/auth timing accumulators. All of it is arithmetic over
// microseconds, so a board's metrics.cpp is left with only the parts that
// genuinely differ -- where cycles come from, and which RAM figures the chip
// can actually report.
//
// Header-only on purpose. Exactly one directory under src/board/ is compiled
// into any binary, and this is the one piece both of them would otherwise have
// written out twice; as a header it stays out of every build_src_filter and
// adds no translation unit to either board.
class Counters {
 public:
  // `nowMicros` starts the first rate window; pass the board's micros().
  void begin(uint32_t nowMicros) { windowStartMicros_ = nowMicros; }

  void markLoop() { ++loops_; }

  void recordRequest(uint32_t elapsedMicros) {
    ++requests_;
    requestTotalMicros_ += elapsedMicros;
    busyMicros_ += elapsedMicros;
    requestMaxMicros_ = std::max(requestMaxMicros_, elapsedMicros);
  }

  void recordAuth(uint32_t elapsedMicros) {
    ++auths_;
    authTotalMicros_ += elapsedMicros;
    authMaxMicros_ = std::max(authMaxMicros_, elapsedMicros);
  }

  // Recomputes the one-second window, or does nothing if it has not closed
  // yet. Unsigned subtraction keeps the elapsed span correct across the
  // rollover of a 32-bit microsecond clock.
  void tick(uint32_t nowMicros) {
    const uint32_t elapsed = nowMicros - windowStartMicros_;
    if (elapsed < kWindowMicros) return;

    loopHz_ = static_cast<uint32_t>(static_cast<uint64_t>(loops_) * 1000000ULL / elapsed);
    busyPermille_ = static_cast<uint32_t>(static_cast<uint64_t>(busyMicros_) * 1000ULL / elapsed);

    loops_ = 0;
    busyMicros_ = 0;
    windowStartMicros_ = nowMicros;
  }

  // Fills the CPU half of a snapshot. The RAM half stays the board's job.
  void fill(Snapshot &out) const {
    out.loopHz = loopHz_;
    out.busyPermille = busyPermille_;
    out.requests = requests_;
    out.requestAvgMicros = requests_ != 0 ? requestTotalMicros_ / requests_ : 0;
    out.requestMaxMicros = requestMaxMicros_;
    out.authAvgMicros = auths_ != 0 ? authTotalMicros_ / auths_ : 0;
    out.authMaxMicros = authMaxMicros_;
  }

 private:
  static constexpr uint32_t kWindowMicros = 1000000;

  uint32_t windowStartMicros_ = 0;
  uint32_t loops_ = 0;
  uint32_t busyMicros_ = 0;

  uint32_t loopHz_ = 0;
  uint32_t busyPermille_ = 0;

  uint32_t requests_ = 0;
  uint32_t requestTotalMicros_ = 0;
  uint32_t requestMaxMicros_ = 0;

  uint32_t auths_ = 0;
  uint32_t authTotalMicros_ = 0;
  uint32_t authMaxMicros_ = 0;
};

}  // namespace metrics

#endif  // METRICS_ENABLED
