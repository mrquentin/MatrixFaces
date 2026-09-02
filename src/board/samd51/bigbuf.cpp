#include "board/bigbuf.h"

// Statically reserved, because on this board there is no alternative: the heap
// and the stack grow towards each other in 192 KB of SRAM, and a 32 KB
// allocation made at runtime would move the collision point somewhere the
// build cannot see. Reserved up front it shows in the build's RAM figure and
// in metrics::snapshot()'s static total, where it can be argued about.
//
// The size is the one the firmware has always used: enough for
// TrackStatus/LapCount/DriverList (a few KB) plus RaceControlMessages, which
// grows all session and dominates. If a session ever overruns it, the fields
// ordered ahead of RaceControlMessages in the query still parse, and the
// `truncated` counter says so out loud.
namespace {

constexpr size_t kResponseCap = 32768;
char g_response[kResponseCap];

}  // namespace

namespace bigbuf {

char *responseBuffer(size_t &outCap) {
  outCap = sizeof(g_response);
  return g_response;
}

}  // namespace bigbuf
