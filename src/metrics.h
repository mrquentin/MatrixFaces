#pragma once

#include <cstddef>
#include <cstdint>

// Compile-time switch. Build with -DMETRICS_ENABLED=0 to remove the
// instrumentation entirely: every entry point below becomes an inline no-op and
// metrics.cpp compiles to nothing, so neither flash nor RAM is spent on it.
#ifndef METRICS_ENABLED
#define METRICS_ENABLED 1
#endif

// Runtime CPU and RAM instrumentation.
//
// RAM: the build-time figure PlatformIO prints only covers statically allocated
// bytes, and it omits .hsram at that. It says nothing about how deep the stack
// actually goes, which is what determines whether this program collides with
// itself. begin() paints the unused region between the heap and the stack so the
// peak can be recovered.
//
// CPU: with no RTOS there is no idle task to measure against, so "busy" here
// means the share of wall time spent inside request handling. The polling rate
// is the complementary signal: it falls when the board is under load.
namespace metrics {

#if METRICS_ENABLED

void begin();

// Free-running Cortex-M4 cycle counter, or 0 if the core does not implement it.
// Wraps every ~35s at 120 MHz, which is far longer than anything timed here.
uint32_t cycles();
uint32_t cyclesToMicros(uint32_t elapsedCycles);
bool cycleCounterAvailable();

// Call once per loop() iteration.
void markLoop();

// Recomputes the one-second rate window. Call from loop().
void tick();

void recordRequest(uint32_t elapsedCycles);
void recordAuth(uint32_t elapsedCycles);

struct Snapshot {
  // CPU
  uint32_t loopHz;
  uint32_t busyPermille;  // parts per thousand, avoids dragging in floats
  uint32_t requests;
  uint32_t requestAvgMicros;
  uint32_t requestMaxMicros;
  uint32_t authAvgMicros;
  uint32_t authMaxMicros;

  // RAM, all in bytes
  uint32_t ramTotal;
  uint32_t ramStatic;
  uint32_t heapUsed;
  uint32_t stackPeak;
  uint32_t freeNow;
  // Smallest gap ever seen between the top of the heap and the deepest the
  // stack has reached. This is the headroom number that matters.
  uint32_t minFreeEver;
};

Snapshot snapshot();

#else

// Every call site collapses to nothing. Timing expressions such as
// `recordRequest(cycles() - start)` fold to a no-op taking a constant, so the
// surrounding code needs no #if of its own.
inline void begin() {}
inline uint32_t cycles() { return 0; }
inline uint32_t cyclesToMicros(uint32_t) { return 0; }
inline bool cycleCounterAvailable() { return false; }
inline void markLoop() {}
inline void tick() {}
inline void recordRequest(uint32_t) {}
inline void recordAuth(uint32_t) {}

#endif  // METRICS_ENABLED

}  // namespace metrics
