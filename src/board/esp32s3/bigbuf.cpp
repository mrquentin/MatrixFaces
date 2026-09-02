#include "board/bigbuf.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

// Allocated out of PSRAM, which is the whole point: 2 MB of it sits on this
// board doing nothing, and spending it here leaves the internal heap -- the
// scarce pool, shared with WiFi, lwIP and every task stack -- untouched.
//
// Twice the M4's size while costing the firmware nothing it was otherwise
// going to use. RaceControlMessages grows for a whole session and is what
// eventually overruns the M4's buffer; the headroom here pushes that out of
// reach for any realistic race.
//
// PSRAM is slower than internal SRAM, which does not matter for this: the
// buffer is written once per poll at network speed and scanned once by the
// parser, neither of which is anywhere near memory-bound.
namespace {

constexpr size_t kResponseCap = 65536;

char *g_response = nullptr;
size_t g_capacity = 0;

}  // namespace

namespace bigbuf {

char *responseBuffer(size_t &outCap) {
  if (g_response == nullptr) {
    g_response = static_cast<char *>(heap_caps_malloc(kResponseCap, MALLOC_CAP_SPIRAM));
    g_capacity = g_response != nullptr ? kResponseCap : 0;

    if (g_response == nullptr) {
      // Deliberately not falling back to the internal heap. Quietly taking
      // 64 KB of the pool WiFi and the task stacks share would trade a visible
      // failure for an intermittent one somewhere else entirely; the caller
      // drops the app that needed this instead.
      Serial.println(F("[bigbuf] FATAL: no PSRAM for the MultiViewer buffer"));
    }
  }

  outCap = g_capacity;
  return g_response;
}

}  // namespace bigbuf
