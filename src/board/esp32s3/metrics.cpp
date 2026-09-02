#include "board/metrics.h"

#if METRICS_ENABLED

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board/metrics_counters.h"

extern "C" {
// Provided by the linker script: the bottom of this image's .data and the top
// of its .bss. Their difference is the statically allocated DRAM, and it is
// the same figure the build prints as "RAM: used N bytes". These names belong
// to the linker, so they cannot be renamed to satisfy the reserved-identifier
// check.
extern char _data_start;       // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern char _static_data_end;  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
}

// The same questions the M4 answers, asked of a chip that keeps the books
// itself. There is nothing to paint here: FreeRTOS already records how deep
// each task's stack has been, and the heap allocator already remembers the
// least free it has ever had, so this file reads those out instead of
// reconstructing them from a pattern in RAM. What that costs is scope --
// stackPeak is the loop task's stack, not the whole program's -- and
// docs/metrics.md says so next to the M4's numbers.
namespace metrics {
namespace {

// Byte-addressable internal memory: the pool everything that is not explicitly
// asked for in PSRAM comes from, and therefore the one worth watching.
//
// Note this is bigger than the DRAM segment the statics live in -- the S3 also
// lends the allocator its unused IRAM and RTC RAM, which are byte-accessible
// here. So the heap's total and the static footprint are two separate
// quantities that do not sum to "all of SRAM", and no runtime number on this
// chip means what the M4's ramTotal means. docs/metrics.md says so.
constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

Counters counters;

// Read once: changing the CPU frequency at runtime would invalidate every
// timing already recorded, so the firmware does not.
uint32_t cpuMhz = 240;

}  // namespace

void begin() {
  cpuMhz = getCpuFrequencyMhz();
  counters.begin(micros());
}

bool cycleCounterAvailable() { return true; }

// Per-core counter. Everything timed here runs on the loop task, which the
// board's build pins to core 1, so the two ends of a measurement always come
// from the same counter. Phase 4 splits the work across cores and will have to
// respect that.
uint32_t cycles() { return ESP.getCycleCount(); }

uint32_t cyclesToMicros(uint32_t elapsedCycles) { return elapsedCycles / cpuMhz; }

void markLoop() { counters.markLoop(); }

void recordRequest(uint32_t elapsedCycles) { counters.recordRequest(cyclesToMicros(elapsedCycles)); }

void recordAuth(uint32_t elapsedCycles) { counters.recordAuth(cyclesToMicros(elapsedCycles)); }

void tick() { counters.tick(micros()); }

Snapshot snapshot() {
  Snapshot out{};
  counters.fill(out);

  out.ramTotal = heap_caps_get_total_size(kInternalCaps);
  out.freeNow = heap_caps_get_free_size(kInternalCaps);
  out.heapUsed = out.ramTotal - out.freeNow;
  out.minFreeEver = heap_caps_get_minimum_free_size(kInternalCaps);

  out.ramStatic = static_cast<uint32_t>(&_static_data_end - &_data_start);

  // FreeRTOS reports the *least free* the stack has ever been, in bytes.
  const uint32_t loopStack = getArduinoLoopTaskStackSize();
  const uint32_t everFree = uxTaskGetStackHighWaterMark(nullptr);
  out.stackPeak = loopStack > everFree ? loopStack - everFree : 0;

  out.psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  out.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  return out;
}

}  // namespace metrics

#endif  // METRICS_ENABLED
