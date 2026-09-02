#include "board/metrics.h"

#if METRICS_ENABLED

#include <Arduino.h>

#include "board/metrics_counters.h"

extern "C" {
// Provided by the linker script: the top of RAM, and the first address above
// all statically allocated data (where the heap starts growing up). These names
// belong to the linker, so they cannot be renamed to satisfy the
// reserved-identifier check.
extern char __StackTop;  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern char __end__;     // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
char *sbrk(int increment);
}

namespace metrics {
namespace {

constexpr uint8_t kPaintByte = 0xc5;
// Leaves room for begin()'s own stack frame so painting cannot clobber it.
constexpr size_t kPaintSafetyMargin = 256;
constexpr uint32_t kRamBase = 0x20000000;

uint8_t *paintFloor = nullptr;
bool cycleCounter = false;

Counters counters;

// Owned by main.cpp and mirrored here so snapshot() can report them without
// metrics needing to know what a setting or an event is.
uint32_t persistWrites = 0;
uint32_t eventsDropped = 0;

char *heapTop() { return sbrk(0); }

// Reading the stack pointer register is well defined, unlike doing pointer
// arithmetic on the address of a local, which GCC flags as out of bounds.
uint8_t *stackPointer() { return reinterpret_cast<uint8_t *>(__get_MSP()); }

}  // namespace

void begin() {
  // Enable the DWT cycle counter. NOCYCCNT means the core omitted it, in which
  // case callers fall back to micros().
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0) {
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cycleCounter = true;
  }

  auto *from = reinterpret_cast<uint8_t *>(heapTop());
  const uint8_t *to = stackPointer() - kPaintSafetyMargin;
  paintFloor = from;
  while (from < to) {
    *from++ = kPaintByte;
  }

  counters.begin(micros());
}

bool cycleCounterAvailable() { return cycleCounter; }

uint32_t cycles() { return cycleCounter ? DWT->CYCCNT : 0; }

uint32_t cyclesToMicros(uint32_t elapsedCycles) {
  return elapsedCycles / (F_CPU / 1000000UL);
}

void markLoop() { counters.markLoop(); }

void recordPersistWrites(uint32_t writes) { persistWrites = writes; }

void recordEventsDropped(uint32_t dropped) { eventsDropped = dropped; }

void recordRequest(uint32_t elapsedCycles) { counters.recordRequest(cyclesToMicros(elapsedCycles)); }

void recordAuth(uint32_t elapsedCycles) { counters.recordAuth(cyclesToMicros(elapsedCycles)); }

void tick() { counters.tick(micros()); }

Snapshot snapshot() {
  Snapshot out{};
  counters.fill(out);

  out.persistWrites = persistWrites;
  out.eventsDropped = eventsDropped;

  char *heap = heapTop();

  out.ramTotal = reinterpret_cast<uint32_t>(&__StackTop) - kRamBase;
  out.ramStatic = reinterpret_cast<uint32_t>(&__end__) - kRamBase;
  out.heapUsed = static_cast<uint32_t>(heap - &__end__);
  out.freeNow = static_cast<uint32_t>(stackPointer() - reinterpret_cast<uint8_t *>(heap));

  // Walk up from the current top of the heap counting bytes still holding the
  // paint pattern. The first disturbed byte is as deep as the stack has ever
  // gone, and everything below it has been reclaimed by the heap.
  const auto *scan = reinterpret_cast<const uint8_t *>(heap);
  if (paintFloor != nullptr && scan < paintFloor) scan = paintFloor;

  uint32_t untouched = 0;
  const auto *limit = reinterpret_cast<const uint8_t *>(&__StackTop);
  while (scan < limit && *scan == kPaintByte) {
    ++scan;
    ++untouched;
  }

  out.minFreeEver = untouched;
  out.stackPeak = static_cast<uint32_t>(limit - scan);
  return out;
}

}  // namespace metrics

#endif  // METRICS_ENABLED
