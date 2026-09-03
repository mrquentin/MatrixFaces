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

  // RAM, all in bytes. What each figure is measured from differs by board --
  // a painted region between heap and stack on the SAMD51, the allocator's own
  // accounting on the ESP32-S3 -- but the question each answers is the same.
  // docs/metrics.md spells out the difference.
  uint32_t ramTotal;
  uint32_t ramStatic;
  uint32_t heapUsed;
  uint32_t stackPeak;
  uint32_t freeNow;
  // The least headroom ever seen: how close the program has come to running
  // out of RAM, rather than how close it is right now. The number that matters.
  uint32_t minFreeEver;

  // External PSRAM, on a board that has any. Left zero otherwise, and
  // /api/metrics then omits the section rather than reporting an empty one.
  uint32_t psramTotal;
  uint32_t psramFree;

  // Bytes still unused on each task's stack, at its worst moment so far.
  // Zero on a board that runs no tasks, and /api/metrics omits the section --
  // this is how the sizes chosen in board/esp32s3/exec.cpp are checked rather
  // than trusted, so an undersized one shows up as a number well before it
  // shows up as a crash.
  // Settings writes actually made. Against the number of changes, this is the
  // debounce made visible: fifty in five seconds should be one.
  uint32_t persistWrites;
  // Events dropped because a listener fell behind. Non-zero means the fan-out
  // is not keeping up, which is worth knowing before someone reports that a
  // browser missed an update.
  uint32_t eventsDropped;

  uint32_t renderStackFree;
  uint32_t netStackFree;
  uint32_t mvStackFree;
  uint32_t wsStackFree;
};

// Totals main.cpp owns, handed in rather than reached for: metrics is a board
// module and has no business knowing what a setting is.
void recordPersistWrites(uint32_t writes);
void recordEventsDropped(uint32_t dropped);

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
inline void recordPersistWrites(uint32_t) {}
inline void recordEventsDropped(uint32_t) {}

#endif  // METRICS_ENABLED

}  // namespace metrics
