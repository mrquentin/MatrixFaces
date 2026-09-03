#include "board/exec.h"

// One core, one thread of control: the four jobs take turns, in the order
// they are listed. This is exactly the loop() this board has always run, and
// its known cost stands -- a MultiViewer poll or a slow HTTP client stalls the
// display until it finishes. Fixing that needs a second core, which this board
// does not have, so it is documented rather than pretended away.
namespace exec {

void start(const Ticks &) {}

void tick(const Ticks &ticks, uint32_t nowMs) {
  // Render first, so a mutation queued by the request served at the end of the
  // previous pass is in effect before anything else looks at app state.
  if (ticks.render != nullptr) ticks.render(nowMs);
  if (ticks.housekeep != nullptr) ticks.housekeep(nowMs);
  if (ticks.mv != nullptr) ticks.mv(nowMs);
  if (ticks.ws != nullptr) ticks.ws(nowMs);
  if (ticks.net != nullptr) ticks.net(nowMs);
}

}  // namespace exec
